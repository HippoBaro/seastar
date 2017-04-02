//
// Created by hbarraud on 4/2/17.
//

#ifndef SEASTAR_WEBSOCKET_FRAGMENT_HH
#define SEASTAR_WEBSOCKET_FRAGMENT_HH

#include <core/reactor.hh>

namespace httpd {

    class websocket_fragment_base {
    public:
        enum opcode : uint8_t {
            CONTINUATION = 0x0,
            TEXT = 0x1,
            BINARY = 0x2,
            CLOSE = 0x8,
            PING = 0x9,
            PONG = 0xA,
            RESERVED = 0xB
        };

    protected:

        bool _fin;
        enum opcode _opcode = RESERVED;
        uint64_t _lenght;
        bool _rsv2;
        bool _rsv3;
        bool _rsv1;
        bool _masked;
        temporary_buffer<char> _maskkey;

    public:
        temporary_buffer<char> message;

        websocket_fragment_base() = default;

        websocket_fragment_base(websocket_fragment_base &) = delete;

        websocket_fragment_base(websocket_fragment_base &&fragment) noexcept : _fin(fragment.fin()),
                                                                               _opcode(fragment.opcode()),
                                                                               _lenght(fragment.length()),
                                                                               _rsv2(fragment.rsv2()),
                                                                               _rsv3(fragment.rsv3()),
                                                                               _rsv1(fragment.rsv1()),
                                                                               _masked(fragment.masked()),
                                                                               _maskkey(std::move(fragment._maskkey)),
                                                                               message(std::move(fragment.message)) {
        }

    public:
        bool fin() { return _fin; }

        opcode opcode() { return _opcode; }

        uint64_t &length() { return _lenght; }

        bool rsv2() { return _rsv2; }

        bool rsv3() { return _rsv3; }

        bool rsv1() { return _rsv1; }

        bool masked() { return _masked; }

        bool valid() {
            return !((rsv1() /*&& !setCompressed(user)*/) || rsv2() || rsv3() || (opcode() > 2 && opcode() < 8) ||
                     opcode() > 10 || (opcode() > 2 && (!fin() || length() > 125)));
        }
    };

    class inbound_websocket_fragment : public websocket_fragment_base {

    public:
        inbound_websocket_fragment() = delete;
        inbound_websocket_fragment(inbound_websocket_fragment &) = delete;
        inbound_websocket_fragment(inbound_websocket_fragment &&other) : websocket_fragment_base(std::move(other)) {
        }

        inbound_websocket_fragment(temporary_buffer<char> &&raw);
    };

    class outbound_websocket_fragment : public websocket_fragment_base {

    public:

        outbound_websocket_fragment() = delete;
        outbound_websocket_fragment(outbound_websocket_fragment &) = delete;
        outbound_websocket_fragment(outbound_websocket_fragment &&other) : websocket_fragment_base(std::move(other)) {
        }
        outbound_websocket_fragment(opcode opcode, temporary_buffer<char> &&response) : message(std::move(response)),
                                                                                        _lenght(response.size()),
                                                                                        _fin(true), _masked(false) {
        }

        temporary_buffer<char> get_header();

        void set_fin(bool fin) { _fin = fin; }

        void set_opcode(opcode opcode) { _opcode = opcode; };

        void set_rsv2(bool rsv2) { _rsv2 = rsv2; } ;

        void set_rsv3(bool rsv3) { _rsv3 = rsv3; };

        void set_rsv1(bool rsv1) { _rsv1 = rsv1; };

    private:
        char get_header_internal() {
            std::bitset<8> firstpart;
            firstpart.set();
            firstpart[7] = _fin;
            firstpart[6] = _rsv1;
            firstpart[5] = _rsv2;
            firstpart[4] = _rsv3;
            std::bitset<8> secondpart(_opcode);
            firstpart.set(7).set(6).set(5).set(4);
            return (char) (firstpart ^ secondpart).to_ulong();
        }
    };

}

#endif //SEASTAR_WEBSOCKET_FRAGMENT_HH
