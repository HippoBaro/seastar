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

    class websocket_fragment_base {
    protected:

        bool _fin = false;
        websocket_opcode _opcode = RESERVED;
        uint64_t _lenght = 0;
        bool _rsv2 = false;
        bool _rsv3 = false;
        bool _rsv1 = false;
        bool _masked = false;
        temporary_buffer<char> _maskkey;

    public:
        bool _is_empty = false;
        temporary_buffer<char> message;
        bool _is_valid = false;

        websocket_fragment_base() : _is_empty(true), _is_valid(false) {}

        websocket_fragment_base(websocket_fragment_base &&fragment) noexcept : _fin(fragment.fin()),
                                                                               _opcode(fragment.opcode()),
                                                                               _lenght(fragment.length()),
                                                                               _rsv2(fragment.rsv2()),
                                                                               _rsv3(fragment.rsv3()),
                                                                               _rsv1(fragment.rsv1()),
                                                                               _masked(fragment.masked()),
                                                                               _maskkey(std::move(fragment._maskkey)),
                                                                               _is_empty(fragment._is_empty),
                                                                               message(std::move(fragment.message)),
                                                                               _is_valid(fragment._is_valid) {
        }

        bool fin() { return _fin; }

        websocket_opcode opcode() { return _opcode; }

        uint64_t &length() { return _lenght; }

        bool rsv2() { return _rsv2; }

        bool rsv3() { return _rsv3; }

        bool rsv1() { return _rsv1; }

        bool masked() { return _masked; }

        bool valid() {
            return !((rsv1() || rsv2() || rsv3() || (opcode() > 2 && opcode() < 8) ||
                     opcode() > 10 || (opcode() > 2 && (!fin() || length() > 125))));
        }

        operator bool() { return !_is_empty && valid(); }
    };

    class inbound_websocket_fragment : public websocket_fragment_base {

    public:

        inbound_websocket_fragment(const inbound_websocket_fragment &) = delete;

        inbound_websocket_fragment(inbound_websocket_fragment &&other) noexcept : websocket_fragment_base(std::move(other)) {
        }

        inbound_websocket_fragment(temporary_buffer<char> &raw, uint32_t *index);

        inbound_websocket_fragment() : websocket_fragment_base() {}

    private:
        inline void unmask(char *dst, const char *src, const char *mask, uint64_t length);
    };

    class websocket_message {
    public:
        websocket_opcode opcode = RESERVED;
        char _header[10];
        size_t _header_size = 0;
        std::vector<temporary_buffer<char>> _fragments;
        temporary_buffer<char> _concatenated;

        websocket_message() noexcept : _is_empty(true) { }
        websocket_message(const websocket_message &) = delete;
        websocket_message(websocket_message &&other) noexcept : opcode(other.opcode),
                                                                _fragments(std::move(other._fragments)),
                                                                _concatenated(std::move(other._concatenated)),
                                                                _is_empty(other._is_empty)  {
        }

        void operator=(const websocket_message&) = delete;
        websocket_message & operator= (websocket_message &&other) {
            if (this != &other) {
                opcode = other.opcode;
                _fragments = std::move(other._fragments);
                _concatenated = std::move(other._concatenated);
                _is_empty = other._is_empty;
            }
            return *this;
        }

        websocket_message(std::unique_ptr<websocket_fragment_base> fragment) noexcept : _is_empty(false) {
            _fragments.push_back(std::move(fragment->message));
            opcode = fragment->opcode();
        }

        websocket_message(websocket_opcode kind, temporary_buffer<char> message) noexcept : _is_empty(false) {
            _fragments.push_back(std::move(message));
            opcode = kind;
        }

        websocket_message(websocket_opcode kind, sstring message) noexcept : _is_empty(false) {
            _fragments.push_back(std::move(message).release());
            opcode = kind;
        }

        void append(std::unique_ptr<websocket_fragment_base> fragment) {
            _fragments.push_back(std::move(fragment->message));
        }

        void done();

        temporary_buffer<char> & concat();

        bool empty() { return _is_empty || opcode == CLOSE; }

    private:
        bool _is_empty;
    };
}

#endif //SEASTAR_WEBSOCKET_FRAGMENT_HH
