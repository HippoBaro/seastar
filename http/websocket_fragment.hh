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


        websocket_fragment_base() : _is_empty(true) {}

        websocket_fragment_base(websocket_fragment_base &&fragment) noexcept : _fin(fragment.fin()),
                                                                               _opcode(fragment.opcode()),
                                                                               _lenght(fragment.length()),
                                                                               _rsv2(fragment.rsv2()),
                                                                               _rsv3(fragment.rsv3()),
                                                                               _rsv1(fragment.rsv1()),
                                                                               _masked(fragment.masked()),
                                                                               _maskkey(std::move(fragment._maskkey)),
                                                                               _is_empty(fragment._is_empty),
                                                                               message(std::move(fragment.message)) {
        }

    public:
        bool fin() { return _fin; }

        websocket_opcode opcode() { return _opcode; }

        uint64_t &length() { return _lenght; }

        bool rsv2() { return _rsv2; }

        bool rsv3() { return _rsv3; }

        bool rsv1() { return _rsv1; }

        bool masked() { return _masked; }

        bool valid() {
            return !((rsv1() /*&& !setCompressed(user)*/) || rsv2() || rsv3() || (opcode() > 2 && opcode() < 8) ||
                     opcode() > 10 || (opcode() > 2 && (!fin() || length() > 125)));
        }

        operator bool() const { return !_is_empty; }
    };

    class inbound_websocket_fragment : public websocket_fragment_base {

    public:
        inbound_websocket_fragment(inbound_websocket_fragment &&other) noexcept : websocket_fragment_base(std::move(other)) {
        }

        inbound_websocket_fragment(temporary_buffer<char> raw);

        inbound_websocket_fragment() : websocket_fragment_base() {}
    };

    class outbound_websocket_fragment : public websocket_fragment_base {

    public:

        outbound_websocket_fragment(outbound_websocket_fragment &&other) noexcept : websocket_fragment_base(std::move(other)) {
        }
        outbound_websocket_fragment(websocket_opcode opcode, temporary_buffer<char> response) {
            _opcode = opcode;
            message = std::move(response);
            _lenght = message.size();
            _fin = true;
            _masked = false;
            _rsv1 = false;
            _rsv2 = false;
            _rsv3 = false;
        }

        temporary_buffer<char> get_header();

        void set_fin(bool fin) { _fin = fin; }

        void set_opcode(websocket_opcode opcode) { _opcode = opcode; };

        void set_rsv2(bool rsv2) { _rsv2 = rsv2; } ;

        void set_rsv3(bool rsv3) { _rsv3 = rsv3; };

        void set_rsv1(bool rsv1) { _rsv1 = rsv1; };

    private:
        char get_header_internal() {
            std::bitset<8> firstpart;
            firstpart.set();
            firstpart[7] = fin();
            firstpart[6] = rsv1();
            firstpart[5] = rsv2();
            firstpart[4] = rsv3();
            std::bitset<8> secondpart(opcode());
            secondpart.set(7).set(6).set(5).set(4);
            return static_cast<unsigned char>((firstpart & secondpart).to_ulong());
        }
    };

}

#endif //SEASTAR_WEBSOCKET_FRAGMENT_HH
