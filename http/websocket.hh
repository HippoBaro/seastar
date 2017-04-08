//
// Created by hippolyteb on 3/9/17.
//

#ifndef SEASTARPLAYGROUND_WEBSOCKET_HPP
#define SEASTARPLAYGROUND_WEBSOCKET_HPP

#include <core/reactor.hh>
#include <net/socket_defs.hh>
#include "websocket_fragment.hh"
#include "request.hh"

namespace httpd {

class websocket_output_stream final {
    output_stream<char> _stream;
    temporary_buffer<char> _buf;
    size_t _size = 0;
public:
    websocket_output_stream() = default;

    websocket_output_stream(output_stream<char> stream) : _stream(std::move(stream)) {}

    websocket_output_stream(websocket_output_stream &&) = default;

    websocket_output_stream &operator=(websocket_output_stream &&) = default;

    future<> write(websocket_opcode kind, temporary_buffer<char>);
    future<> write(websocket_opcode kind, sstring buf);
    future<> close() { return _stream.close(); };
private:
    friend class reactor;
};

class websocket_input_stream final {
    input_stream<char> _stream;
    websocket_message _lastmassage;
    bool _eof = false;
private:
    using tmp_buf = temporary_buffer<char>;

private:
    future<websocket_message> read_message();

public:
    websocket_input_stream() = default;

    explicit websocket_input_stream(input_stream<char> stream) : _stream(std::move(stream)) {}

    websocket_input_stream(websocket_input_stream &&) = default;

    websocket_input_stream &operator=(websocket_input_stream &&) = default;

    bool eof() { return _eof; }

    future<inbound_websocket_fragment> read_fragment();

    future<temporary_buffer<char>> read();

    future<> close() { return _stream.close(); }
};

class connected_websocket {
private:
    connected_socket *_socket;

public:
    socket_address remote_adress;
    request _request;

    connected_websocket(connected_socket *socket, socket_address &remote_adress, request &request) noexcept;

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