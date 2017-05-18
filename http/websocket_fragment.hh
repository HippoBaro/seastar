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
    bool utf8_check(const unsigned char *s, size_t length);

    class inbound_websocket_fragment_base {
        friend class websocket_input_stream_base;
    protected:
        //todo make bool members part of a bit field/bitset
        websocket_opcode _opcode;
        bool _rsv2 = false;
        bool _rsv3 = false;
        bool _rsv1 = false;
        uint64_t _length = 0;

    public:
        uint32_t mask_key = 0;
        bool fin = false;
        temporary_buffer<char> message;

        inbound_websocket_fragment_base(temporary_buffer<char> &raw, uint32_t *i) noexcept ;

        inbound_websocket_fragment_base(const inbound_websocket_fragment_base &) = delete;
        inbound_websocket_fragment_base(inbound_websocket_fragment_base &&fragment) noexcept :
                _opcode(fragment._opcode),
                _rsv2(fragment._rsv2),
                _rsv3(fragment._rsv3),
                _rsv1(fragment._rsv1),
                mask_key(fragment.mask_key),
                fin(fragment.fin),
                message(std::move(fragment.message)) {
        }

        inbound_websocket_fragment_base() = default;

        inbound_websocket_fragment_base& operator=(const inbound_websocket_fragment_base&) = delete;
        inbound_websocket_fragment_base& operator=(inbound_websocket_fragment_base &&fragment) noexcept {
            if (*this != fragment) {
                _opcode = fragment._opcode;
                _rsv2 = fragment._rsv2;
                _rsv3 = fragment._rsv3;
                _rsv1 = fragment._rsv1;
                fin = fragment.fin;
                mask_key = fragment.mask_key;
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
            fin = false;
            mask_key = 0;
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
        inbound_websocket_fragment() noexcept {}

        inbound_websocket_fragment(temporary_buffer<char> &raw, uint32_t *i) noexcept:
                inbound_websocket_fragment_base(raw, i) {
            if (raw.size() >= *i + _length) {
                message = std::move(raw.share(*i, _length));
                *i += _length;
            }
        }
    };

    template<>
    class inbound_websocket_fragment<SERVER> final : public inbound_websocket_fragment_base {
        using inbound_websocket_fragment_base::inbound_websocket_fragment_base;
    public:
        inbound_websocket_fragment() {}

        inbound_websocket_fragment(temporary_buffer<char> &raw, uint32_t *i) noexcept:
                inbound_websocket_fragment_base(raw, i) {
            if (raw.size() >= *i + _length + sizeof(uint32_t)) { //message is masked
                mask_key = *reinterpret_cast<uint32_t *>(raw.get_write()  + *i);
                *i += sizeof(uint32_t);
                message = std::move(raw.share(*i, _length));
                *i += _length;
            }
        }
    };
}

#endif //SEASTAR_WEBSOCKET_FRAGMENT_HH
