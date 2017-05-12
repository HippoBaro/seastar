//
// Created by hbarraud on 4/2/17.
//

#ifndef SEASTAR_WEBSOCKET_FRAGMENT_HH
#define SEASTAR_WEBSOCKET_FRAGMENT_HH

#include <core/reactor.hh>

namespace httpd {

    enum websocket_opcode : uint8_t {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA,
        RESERVED = 0xB
    };

    class inbound_websocket_fragment {
        friend class websocket_input_stream;
    protected:
        //todo make bool members part of a bit field/bitset
        bool fin = false;
        websocket_opcode _opcode;
        bool _rsv2 = false;
        bool _rsv3 = false;
        bool _rsv1 = false;
        bool _masked = false;

        inbound_websocket_fragment(temporary_buffer<char> &raw, uint32_t *index);

    public:
        temporary_buffer<char> message;

        inbound_websocket_fragment(const inbound_websocket_fragment &) = delete;

        inbound_websocket_fragment(inbound_websocket_fragment &&fragment) noexcept : fin(fragment.fin),
                                                                                  _opcode(fragment._opcode),
                                                                                  _rsv2(fragment._rsv2),
                                                                                  _rsv3(fragment._rsv3),
                                                                                  _rsv1(fragment._rsv1),
                                                                                  _masked(fragment._masked),
                                                                                  message(std::move(fragment.message)) {
        }

        inbound_websocket_fragment() = default;

        inbound_websocket_fragment& operator=(const inbound_websocket_fragment&) = delete;
        inbound_websocket_fragment& operator=(inbound_websocket_fragment &&fragment) {
            if (*this != fragment) {
                fin = fragment.fin;
                _opcode = fragment._opcode;
                _rsv2 = fragment._rsv2;
                _rsv3 = fragment._rsv3;
                _rsv1 = fragment._rsv1;
                _masked = fragment._masked;
                message = std::move(fragment.message);
            }
            return *this;
        }

        websocket_opcode opcode() { return _opcode; }

        void reset() {
            fin = false;
            _opcode = RESERVED;
            _rsv2 = false;
            _rsv3 = false;
            _rsv1 = false;
            _masked = false;
            message = temporary_buffer<char>();
        }

        operator bool() { return !message.empty() && !((_rsv1 || _rsv2 || _rsv3 || (_opcode > 2 && _opcode < 8) ||
                    _opcode > 10 || (_opcode > 2 && (!fin || message.size() > 125)))); }

    private:
        inline void unmask(char *dst, const char *src, const char *mask, uint64_t length);
    };

    class websocket_message {
        friend class websocket_output_stream;
    private:
        temporary_buffer<char> _concatenated;
    public:
        websocket_opcode opcode = RESERVED;
        int _header_size = 0;
        std::array<char, 10> _header;
        std::vector<temporary_buffer<char>> _fragments;

        websocket_message() = default;
        websocket_message(const websocket_message &) = delete;
        websocket_message(websocket_message &&other) noexcept : _concatenated(std::move(other._concatenated)),
                                                                opcode(other.opcode),
                                                                _header_size(other._header_size),
                                                                _header(other._header),
                                                                _fragments(std::move(other._fragments)) {
        }

        void operator=(const websocket_message&) = delete;
        websocket_message & operator= (websocket_message &&other) {
            if (this != &other) {
                _concatenated = std::move(other._concatenated);
                opcode = other.opcode;
                _header_size = other._header_size;
                _header = other._header;
                _fragments = std::move(other._fragments);
            }
            return *this;
        }

        explicit operator bool() const { return !_fragments.empty(); }

        websocket_message(inbound_websocket_fragment fragment) noexcept :
                websocket_message(fragment.opcode(), std::move(fragment.message)) {
        }

        websocket_message(websocket_opcode kind, sstring message) noexcept :
                websocket_message(kind, std::move(message).release()) {
        }

        websocket_message(websocket_opcode kind, temporary_buffer<char> message) noexcept :
                opcode(kind) {
            _fragments.push_back(std::move(message));
        }

        void append(inbound_websocket_fragment fragment) {
            _fragments.push_back(std::move(fragment.message));
        }

        temporary_buffer<char> & concat();

        void reset() {
            opcode = RESERVED;
            _header_size = 0;
            _fragments = std::vector<temporary_buffer<char>>();
            _concatenated = temporary_buffer<char>();
        }

    private:
        void done();
    };
}

#endif //SEASTAR_WEBSOCKET_FRAGMENT_HH
