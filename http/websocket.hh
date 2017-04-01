//
// Created by hippolyteb on 3/9/17.
//

#ifndef SEASTARPLAYGROUND_WEBSOCKET_HPP
#define SEASTARPLAYGROUND_WEBSOCKET_HPP

#include <core/reactor.hh>
#include "core/scattered_message.hh"
#include <net/socket_defs.hh>

namespace httpd {

class websocket_fragment {
public:
    enum opcode : int {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA,
        RESERVED = 0xB
    };

private:

    bool _fin;
    enum opcode _opcode = RESERVED;
    uint64_t _lenght;
    bool _rsv2;
    bool _rsv3;
    bool _rsv1;
    bool _masked;
    temporary_buffer<char> _maskkey;

public:
    temporary_buffer<char> data;

    websocket_fragment(websocket_fragment &&fragment) noexcept : _fin(fragment.fin()),
                                                        _opcode(fragment.opcode()),
                                                        _lenght(fragment.length()),
                                                        _rsv2(fragment.rsv2()),
                                                        _rsv3(fragment.rsv3()),
                                                        _rsv1(fragment.rsv1()),
                                                        _masked(fragment.masked()),
                                                        _maskkey(std::move(fragment._maskkey)),
                                                        data(std::move(fragment.data)) {
    }

    websocket_fragment(temporary_buffer<char> &&raw) {
        uint64_t i = sizeof(uint16_t);

        for (std::size_t i = 0; i < raw.size(); ++i)
            std::cout << std::bitset<8>(raw.begin()[i]) << std::endl;

        std::bitset<8> header(raw.begin()[0]);
        std::cout << "header : " << header << std::endl;
        _fin = header.test(7);
        std::cout << "fin : " << _fin << std::endl;
        _rsv1 = header.test(6);
        std::cout << "rsv1 : " << rsv1() << std::endl;
        _rsv2 = header.test(5);
        std::cout << "rsv2 : " << rsv2() << std::endl;
        _rsv3 = header.test(4);
        std::cout << "rsv3 : " << rsv3() << std::endl;
        _opcode = static_cast<enum opcode>(header.reset(7).reset(6).reset(5).reset(4).to_ulong());
        std::cout << "opcode : " << _opcode << std::endl;

        header = std::bitset<8>(raw.begin()[1]);
        std::cout << "header : " << header << std::endl;
        _masked = header.test(7);
        std::cout << "masked : " << _masked << std::endl;
        header = header.reset(7);
        std::cout << "header : " << header << std::endl;

        if (header.to_ulong() < 126) {
            _lenght = header.to_ulong();
            std::cout << "lenght : " << _lenght << std::endl;
        }
        else if (header.to_ulong() == 126) {
            _lenght = *((uint16_t *) raw.share(i, sizeof(uint16_t)).get());
            std::cout << "lenght : " << _lenght << std::endl;
            i += sizeof(uint16_t);
        }
        else if (header.to_ulong() == 127) {
            _lenght = *((uint64_t *) raw.share(i, sizeof(uint64_t)).get());
            std::cout << "lenght : " << _lenght << std::endl;
            i += sizeof(uint64_t);
        }

        if (_masked) {
            _maskkey = temporary_buffer<char>(std::move(raw.share(i, sizeof(uint32_t))));
            i += sizeof(uint32_t);

            data = temporary_buffer<char>(std::move(raw.share(i, _lenght)));
            for (uint64_t j = 0; j < _lenght; ++j)
            {
                std::cout << "----------" << std::endl;
                std::bitset<8> dat(data[j]);
                std::bitset<8> key(_maskkey[j%4]);
                std::cout << dat << " ^ " << key << " --> " << (dat ^ key) << std::endl;
                std::cout << data[j] << " --> " << (char)(data[j] ^ _maskkey[j%4]) << std::endl;
                data.get_write()[j] = data[j] ^ _maskkey[j%4];
            }
            std::cout << std::endl;
        } else
            data = temporary_buffer<char>(std::move(raw.share(i, _lenght)));
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

class websocket_output_stream final {
    output_stream<char> _stream;
    temporary_buffer<char> _buf;
    size_t _size = 0;
    size_t _begin = 0;
    size_t _end = 0;
    bool _trim_to_size = false;
    bool _batch_flushes = false;
    std::experimental::optional<promise<>> _in_batch;
    bool _flush = false;
    bool _flushing = false;
    std::exception_ptr _ex;
public:
    websocket_output_stream() = default;

    websocket_output_stream(output_stream<char> stream) : _stream(std::move(stream)) {}

    websocket_output_stream(websocket_output_stream &&) = default;

    websocket_output_stream &operator=(websocket_output_stream &&) = default;

    //future<> write(const char* buf, size_t n);
    //future<> write(const char* buf);

    //future<> write(const basic_sstring<StringChar, SizeType, MaxSize>& s);
    //future<> write(const std::basic_string<char>& s);

    //future<> write(net::packet p);
    //future<> write(scattered_message<char> msg);
    future<> write(temporary_buffer<char>);
    future<> flush();
    future<> close() { return _stream.close(); };
private:
    friend class reactor;
};

class websocket_input_stream final {
    input_stream<char> _stream;
    std::string _buf = "";
    bool _eof = false;
private:
    using tmp_buf = temporary_buffer<char>;

    size_t available() const { return _buf.size(); }

protected:
    void reset() { _buf = {}; }

public:
    websocket_input_stream() = default;

    explicit websocket_input_stream(input_stream<char> stream) : _stream(std::move(stream)), _buf("") {}

    websocket_input_stream(websocket_input_stream &&) = default;

    websocket_input_stream &operator=(websocket_input_stream &&) = default;

    bool eof() { return _eof; }

    future<websocket_fragment> readFragment();

    future<temporary_buffer<char>> read();

    future<temporary_buffer<char>> readRaw();

    future<> close() { return _stream.close(); }

    /// Ignores n next bytes from the stream.
    //future<> skip(uint64_t n);
};

class connected_websocket {
private:
    connected_socket *_socket;
public:
    socket_address remote_adress;

    connected_websocket(connected_socket *socket, socket_address &remote_adress) noexcept;

    connected_websocket(connected_websocket &&cs) noexcept;

    connected_websocket &operator=(connected_websocket &&cs) noexcept;

    websocket_input_stream input() {
        return websocket_input_stream(std::move(_socket->input()));
    }

    websocket_output_stream output() {
        return websocket_output_stream(std::move(_socket->output()));
    }
};
}

#endif //SEASTARPLAYGROUND_WEBSOCKET_HPP