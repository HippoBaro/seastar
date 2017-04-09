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

    public:
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
    };

    class outbound_websocket_fragment : public websocket_fragment_base {

    public:

        outbound_websocket_fragment(const outbound_websocket_fragment &) = delete;
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

    class websocket_message {
    public:
        websocket_opcode opcode = RESERVED;
        temporary_buffer<char> _header;
        std::vector<temporary_buffer<char>> _fragments;

        websocket_message() noexcept : _is_empty(true) { }
        websocket_message(const websocket_message &) = delete;
        websocket_message(websocket_message &&other) noexcept : opcode(other.opcode),
                                                                _header(std::move(other._header)),
                                                                _fragments(std::move(other._fragments)),
                                                                _is_empty(other._is_empty) {
        }

        void operator=(const websocket_message&) = delete;
        websocket_message & operator= (websocket_message &&other) {
            if (this != &other) {
                opcode = other.opcode;
                _header = std::move(other._header),
                _fragments = std::move(other._fragments);
                _is_empty = other._is_empty;
            }
            return *this;
        }

        websocket_message(websocket_fragment_base fragment) noexcept : _is_empty(false) {
            _fragments.push_back(std::move(fragment.message));
            opcode = fragment.opcode();
        }

        websocket_message(websocket_opcode kind, temporary_buffer<char> message) noexcept : _is_empty(false) {
            _fragments.push_back(std::move(message));
            opcode = kind;
        }

        websocket_message(websocket_opcode kind, sstring message) noexcept : _is_empty(false) {
            _fragments.push_back(std::move(message).release());
            opcode = kind;
        }

        void append(websocket_fragment_base fragment) {
            _fragments.push_back(std::move(fragment.message));
        }

        void done() {
            auto header = 0x81;

            uint64_t len = 0;
            for (auto &fragment : _fragments)
                len += fragment.size();

            if (len < 125) { //Size fits 7bits
                temporary_buffer<char> buff(2);
                buff.get_write()[0] = header;
                buff.get_write()[1] = static_cast<unsigned char>(len);

                _header = std::move(buff);
            } //Size in extended to 16bits
            else if (len < std::numeric_limits<uint16_t>::max()) {
                temporary_buffer<char> buff(4);
                buff.get_write()[0] = header;
                buff.get_write()[1] = static_cast<unsigned char>(126);
                std::memcpy(buff.share(2, 2).get_write(), &len, 2);

                _header = std::move(buff);
            }
            else { //Size extended to 64bits
                temporary_buffer<char> buff(10);
                buff.get_write()[0] = header;
                std::bitset<8> byte2(126);
                buff.get_write()[1] = static_cast<unsigned char>(127);
                std::memcpy(buff.share(2, 8).get_write(), &len, 8);

                _header = std::move(buff);
            }
        }

        temporary_buffer<char> concat() {
            std::cout << "copying" << std::endl;
            uint64_t length = 0;
            for (auto& n : _fragments)
                length += n.size();
            temporary_buffer<char> ret(length);
            length = 0;
            for (auto& n : _fragments) {
                std::memcpy(ret.share(length, n.size()).get_write(), n.get(), n.size());
                length = n.size();
            }
            return std::move(ret);
        }

        bool empty() { return _is_empty || opcode == CLOSE; }

    private:
        bool _is_empty;
    };
}

#endif //SEASTAR_WEBSOCKET_FRAGMENT_HH
