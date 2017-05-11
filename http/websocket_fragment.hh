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
    protected:
        //todo make bool members part of a bit field/bitset
        bool _fin = false;
        websocket_opcode _opcode = RESERVED;
        uint64_t _lenght = 0;
        bool _rsv2 = false;
        bool _rsv3 = false;
        bool _rsv1 = false;
        bool _masked = false;

    public:
        bool _is_empty = true;
        temporary_buffer<char> message;
        bool _is_valid = false;

        inbound_websocket_fragment(const inbound_websocket_fragment &) = delete;

        inbound_websocket_fragment(inbound_websocket_fragment &&fragment) noexcept : _fin(fragment._fin),
                                                                                  _opcode(fragment._opcode),
                                                                                  _lenght(fragment._lenght),
                                                                                  _rsv2(fragment._rsv2),
                                                                                  _rsv3(fragment._rsv3),
                                                                                  _rsv1(fragment._rsv1),
                                                                                  _masked(fragment._masked),
                                                                                  _is_empty(fragment._is_empty),
                                                                                  message(std::move(fragment.message)),
                                                                                  _is_valid(fragment._is_valid) {
        }

        inbound_websocket_fragment(temporary_buffer<char> &raw, uint32_t *index);

        inbound_websocket_fragment() = default;

        inbound_websocket_fragment& operator=(const inbound_websocket_fragment&) = delete;
        inbound_websocket_fragment& operator=(inbound_websocket_fragment &&fragment) {
            if (*this != fragment) {
                _fin = fragment._fin;
                _opcode = fragment._opcode;
                _lenght = fragment._lenght;
                _rsv2 = fragment._rsv2;
                _rsv3 = fragment._rsv3;
                _rsv1 = fragment._rsv1;
                _masked = fragment._masked;
                _is_empty = fragment._is_empty;
                message = std::move(fragment.message);
                _is_valid= fragment._is_valid;
            }
            return *this;
        }

        bool fin() { return _fin; }
        websocket_opcode opcode() { return _opcode; }
        uint64_t length() { return _lenght; }
        bool rsv2() { return _rsv2; }
        bool rsv3() { return _rsv3; }
        bool rsv1() { return _rsv1; }
        bool masked() { return _masked; }
        bool valid() {
            return !((rsv1() || rsv2() || rsv3() || (opcode() > 2 && opcode() < 8) ||
                      opcode() > 10 || (opcode() > 2 && (!fin() || length() > 125))));
        }

        void reset() {
            _fin = false;
            _opcode = RESERVED;
            _lenght = 0;
            _rsv2 = false;
            _rsv3 = false;
            _rsv1 = false;
            _masked = false;
            _is_empty = false;
            message = temporary_buffer<char>();
            _is_valid = false;
        }

        operator bool() { return !_is_empty && valid(); }

    private:
        inline void unmask(char *dst, const char *src, const char *mask, uint64_t length);
    };

    class websocket_message {
    public:
        websocket_opcode opcode = RESERVED;
        size_t _header_size = 0;
        char _header[10];
        std::vector<temporary_buffer<char>> _fragments;
        temporary_buffer<char> _concatenated;

        websocket_message() noexcept : _is_empty(true) { }
        websocket_message(const websocket_message &) = delete;
        websocket_message(websocket_message &&other) noexcept : opcode(other.opcode),
                                                                _header_size(other._header_size),
                                                                _fragments(std::move(other._fragments)),
                                                                _concatenated(std::move(other._concatenated)),
                                                                _is_empty(other._is_empty)  {
            std::memmove(_header, other._header, other._header_size);
        }

        void operator=(const websocket_message&) = delete;
        websocket_message & operator= (websocket_message &&other) {
            if (this != &other) {
                opcode = other.opcode;
                _header_size = other._header_size;
                std::memmove(_header, other._header, other._header_size);
                _fragments = std::move(other._fragments);
                _concatenated = std::move(other._concatenated);
                _is_empty = other._is_empty;
            }
            return *this;
        }

        explicit operator bool() const { return !_is_empty; }

        websocket_message(inbound_websocket_fragment fragment) noexcept :
                websocket_message(fragment.opcode(), std::move(fragment.message)) {
        }

        websocket_message(websocket_opcode kind, sstring message) noexcept :
                websocket_message(kind, std::move(message).release()) {
        }

        websocket_message(websocket_opcode kind, temporary_buffer<char> message) noexcept :
                opcode(kind), _is_empty(false) {
            _fragments.push_back(std::move(message));
        }

        void append(inbound_websocket_fragment fragment) {
            _fragments.push_back(std::move(fragment.message));
        }

        void done();

        temporary_buffer<char> & concat();

        bool empty() { return _is_empty || opcode == CLOSE; }

        void reset() {
            opcode = RESERVED;
            _header_size = 0;
            _fragments = std::vector<temporary_buffer<char>>();
            _concatenated = temporary_buffer<char>();
            _is_empty = true;
        }

    private:
        bool _is_empty;
    };
}

#endif //SEASTAR_WEBSOCKET_FRAGMENT_HH
