//
// Created by hbarraud on 4/2/17.
//

#ifndef SEASTAR_WEBSOCKET_FRAGMENT_HH
#define SEASTAR_WEBSOCKET_FRAGMENT_HH

#include <core/reactor.hh>
#include <random>

namespace httpd {

    enum websocket_type {
        SERVER,
        CLIENT
    };

    enum websocket_opcode : uint8_t {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA,
        RESERVED = 0xB
    };

    void un_mask(char *dst, const char *src, const char *mask, uint64_t length);

    class inbound_websocket_fragment_base {
        friend class websocket_input_stream_base;
    protected:
        //todo make bool members part of a bit field/bitset
        websocket_opcode _opcode;
        bool _rsv2 = false;
        bool _rsv3 = false;
        bool _rsv1 = false;
        bool _masked = false;
        uint64_t _lenght = 0;

        virtual void parse(temporary_buffer<char>&, uint32_t*);

    public:
        bool fin = false;
        temporary_buffer<char> message;

        inbound_websocket_fragment_base(const inbound_websocket_fragment_base &) = delete;

        inbound_websocket_fragment_base(inbound_websocket_fragment_base &&fragment) noexcept : _opcode(fragment._opcode),
                                                                                  _rsv2(fragment._rsv2),
                                                                                  _rsv3(fragment._rsv3),
                                                                                  _rsv1(fragment._rsv1),
                                                                                  _masked(fragment._masked),
                                                                                     fin(fragment.fin),
                                                                                  message(std::move(fragment.message)) {
        }

        inbound_websocket_fragment_base() = default;

        inbound_websocket_fragment_base& operator=(const inbound_websocket_fragment_base&) = delete;
        inbound_websocket_fragment_base& operator=(inbound_websocket_fragment_base &&fragment) {
            if (*this != fragment) {
                _opcode = fragment._opcode;
                _rsv2 = fragment._rsv2;
                _rsv3 = fragment._rsv3;
                _rsv1 = fragment._rsv1;
                _masked = fragment._masked;
                fin = fragment.fin;
                message = std::move(fragment.message);
            }
            return *this;
        }

        websocket_opcode opcode() { return _opcode; }

        void reset() {
            _opcode = RESERVED;
            _rsv2 = false;
            _rsv3 = false;
            _rsv1 = false;
            _masked = false;
            fin = false;
            message = temporary_buffer<char>();
        }

        operator bool() { return !message.empty() && !((_rsv1 || _rsv2 || _rsv3 || (_opcode > 2 && _opcode < 8) ||
                    _opcode > 10 || (_opcode > 2 && (!fin || message.size() > 125)))); }
    };

    template<websocket_type type>
    class inbound_websocket_fragment final : public inbound_websocket_fragment_base {};

    template<>
    class inbound_websocket_fragment<CLIENT> final : public inbound_websocket_fragment_base {
    using inbound_websocket_fragment_base::inbound_websocket_fragment_base;
    public:
        void parse(temporary_buffer<char> &raw, uint32_t *index) override final {
            inbound_websocket_fragment_base::parse(raw, index);
            if (raw.size() >= *index + _lenght) {
                message = std::move(raw.share(*index, _lenght));
                *index += _lenght;
            }
        }
    };

    template<>
    class inbound_websocket_fragment<SERVER> final : public inbound_websocket_fragment_base {
    using inbound_websocket_fragment_base::inbound_websocket_fragment_base;
    public:
        void parse(temporary_buffer<char> &raw, uint32_t *index) override final {
            inbound_websocket_fragment_base::parse(raw, index);
            if (_masked && raw.size() >= *index + _lenght + sizeof(uint32_t)) { //message is masked
                uint64_t k = *index;
                *index += sizeof(uint32_t);
                message = std::move(raw.share(*index, _lenght));
                un_mask(raw.get_write() + *index, raw.get_write() + *index, raw.get_write() + k, _lenght);
                *index += _lenght;
            }
        }
    };

    class websocket_message_base {
    public:
        websocket_opcode opcode = RESERVED;
        int _header_size = 0;
        std::array<char, 14> _header;
        std::vector<temporary_buffer<char>> payload;

        websocket_message_base() = default;
        websocket_message_base(const websocket_message_base &) = delete;
        websocket_message_base(websocket_message_base &&other) noexcept : opcode(other.opcode),
                                                                _header_size(other._header_size),
                                                                _header(other._header),
                                                                payload(std::move(other.payload)) {
        }

        void operator=(const websocket_message_base&) = delete;
        websocket_message_base & operator= (websocket_message_base &&other) {
            if (this != &other) {
                opcode = other.opcode;
                _header_size = other._header_size;
                _header = other._header;
                payload = std::move(other.payload);
            }
            return *this;
        }

        explicit operator bool() const { return !payload.empty(); }

        websocket_message_base(inbound_websocket_fragment_base fragment) noexcept :
                websocket_message_base(fragment.opcode(), std::move(fragment.message)) {
        }

        websocket_message_base(websocket_opcode kind, sstring message) noexcept :
                websocket_message_base(kind, std::move(message).release()) {
        }

        websocket_message_base(websocket_opcode kind, temporary_buffer<char> message) noexcept :
                opcode(kind) {
            payload.push_back(std::move(message));
        }

        void append(inbound_websocket_fragment_base fragment) {
            payload.emplace_back(fragment.message.begin(), fragment.message.size());
        }

        void reset() {
            opcode = RESERVED;
            _header_size = 0;
            payload.clear();
        }

        uint8_t write_payload_size() {
            assert(_header_size == 0 && "httpd::websocket_message::done() should be called exactly once");

            uint8_t advertised_size = 0;
            const auto header = opcode ^ 0x80; //FIXME Dynamically construct header
            _header[0] = header;

            size_t len = 0;
            for (auto &&item : payload)
                len += item.size();

            if (payload.size() < 125) { //Size fits 7bits
                advertised_size = (uint8_t) len;
                _header_size = 2;
            }
            else if (payload.size() < std::numeric_limits<uint16_t>::max()) { //Size in extended to 16bits
                advertised_size = 126;
                auto s = net::hton(static_cast<uint16_t>(len));
                std::memcpy(_header.data() + sizeof(uint16_t), &s, sizeof(uint16_t));
                _header_size = 4;
            }
            else { //Size extended to 64bits
                advertised_size = 127;
                auto l = net::hton(len);
                std::memcpy(_header.data() + sizeof(uint16_t), &l, sizeof(uint64_t));
                _header_size = 10;
            }
            return advertised_size;
        }
    };

    template<websocket_type type>
    class websocket_message : public websocket_message_base {};

    template<>
    class websocket_message<CLIENT> final : public websocket_message_base {
        using websocket_message_base::websocket_message_base;
    public:
        void done() {
            _header[1] = (char) (128 | write_payload_size());
            //FIXME Constructing an independent_bits_engine is expensive. static thread_local ?
            std::independent_bits_engine<std::default_random_engine, std::numeric_limits<uint32_t>::digits, uint32_t> rbe;
            uint32_t mask = rbe();
            std::memcpy(_header.data() + _header_size, &mask, sizeof(uint32_t));
            _header_size += sizeof(uint32_t);
            //fixme mask

            if (payload.empty())
                return;
            un_mask(payload.front().get_write(), payload.front().get(), (char *) (&mask), payload.front().size());
        }
    };

    template<>
    class websocket_message<SERVER> final : public websocket_message_base {
    public:
        using websocket_message_base::websocket_message_base;
        void done() {
            _header[1] = write_payload_size();
        };
    };
}

#endif //SEASTAR_WEBSOCKET_FRAGMENT_HH
