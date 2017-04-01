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
    temporary_buffer<char> _raw;
    uint16_t _header;

    bool _fin;
    unsigned char _opcode;
    uint64_t _lenght;
    bool _rsv23;
    bool _rsv1;
    bool _masked;

    uint32_t _maskkey;

public:

    temporary_buffer<char> data;

    websocket_fragment(temporary_buffer<char> &&raw) : _raw(std::move(raw)), _header(0) {
        uint64_t i = sizeof(uint16_t);
        std::memcpy(&_header, _raw.begin(), sizeof(uint16_t));
        _fin = _header & 128;
        _opcode = _header & 15;
        _rsv23 = _header & 48;
        _rsv1 = _header & 64;
        _masked = _header & 32768;
        _lenght = (_header >> 8) & 127;
        if (_lenght == 126) {
            _lenght = *((uint16_t *) _raw.share(i, sizeof(uint16_t)).get());
            i += sizeof(uint16_t);
        } else if (_lenght == 127) {
            _lenght = *((uint64_t *) _raw.share(i, sizeof(uint64_t)).get());
            i += sizeof(uint64_t);
        }

        if (_masked) {
            _maskkey = *((uint16_t *) _raw.share(i, sizeof(uint32_t)).get());
            i += sizeof(uint32_t);
        }

        data = _raw.share(i, _lenght);
    }

public:
    bool fin() { return _fin; }

    unsigned char opcode() { return _opcode; }

    uint64_t &length() { return _lenght; }

    bool rsv23() { return _rsv23; }

    bool rsv1() { return _rsv1; }

    bool masked() { return _masked; }

    bool valid() {
        return !((rsv1() /*&& !setCompressed(user)*/) || rsv23() || (opcode() > 2 && opcode() < 8) ||
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