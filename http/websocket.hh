//
// Created by hippolyteb on 3/9/17.
//

#ifndef SEASTARPLAYGROUND_WEBSOCKET_HPP
#define SEASTARPLAYGROUND_WEBSOCKET_HPP

#include <core/reactor.hh>
#include "request.hh"
#include "websocket_fragment.hh"

namespace httpd {

class websocket_output_stream final {
    output_stream<char> _stream;
public:
    websocket_output_stream() = default;

    websocket_output_stream(output_stream<char> stream) : _stream(std::move(stream)) {}

    websocket_output_stream(websocket_output_stream &&) = default;

    websocket_output_stream &operator=(websocket_output_stream &&) = default;

    future<> write(httpd::websocket_message message);
    future<> write(websocket_opcode kind, temporary_buffer<char>);
    future<> write(websocket_opcode kind, sstring buf);
    future<> close() { return _stream.close(); };
private:
    friend class reactor;
};

class websocket_input_stream final {
    input_stream<char> _stream;
    inbound_websocket_fragment _fragment;
    websocket_message _lastmassage;
    temporary_buffer<char> _buf;
    uint32_t _index = 0;

public:
    websocket_input_stream() = default;

    explicit websocket_input_stream(input_stream<char> stream) : _stream(std::move(stream)) {}

    websocket_input_stream(websocket_input_stream &&) = default;

    websocket_input_stream &operator=(websocket_input_stream &&) = default;

    future<httpd::websocket_message> read();

    future<> read_fragment();

    future<> close() { return _stream.close(); }
};

class connected_websocket {
private:
    connected_socket _socket;

public:
    socket_address remote_adress;

    connected_websocket(connected_socket socket, const socket_address remote_adress) noexcept;

    connected_websocket(connected_websocket &&cs) noexcept;

    connected_websocket &operator=(connected_websocket &&cs) noexcept;

    websocket_input_stream input() {
        return websocket_input_stream(std::move(_socket.input()));
    }

    websocket_output_stream output() {
        return websocket_output_stream(std::move(_socket.output()));
    }

    void shutdown_output() {
        _socket.shutdown_output();
    }

    void shutdown_input() {
        _socket.shutdown_input();
    }

    static sstring generate_websocket_key(sstring nonce);
    static future<connected_websocket> connect_websocket(socket_address sa, socket_address local = socket_address(::sockaddr_in{AF_INET, INADDR_ANY, {0}}));
};
}

#endif //SEASTARPLAYGROUND_WEBSOCKET_HPP