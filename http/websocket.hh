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

    future<> write(std::unique_ptr<httpd::websocket_message> message);
    future<> write(websocket_opcode kind, temporary_buffer<char>);
    future<> write(websocket_opcode kind, sstring buf);
    future<> close() { return _stream.close(); };
private:
    friend class reactor;
};

class websocket_input_stream final {
    input_stream<char> _stream;
    std::unique_ptr<inbound_websocket_fragment> _fragment;
    std::unique_ptr<websocket_message> _lastmassage;
    temporary_buffer<char> _buf;
    uint32_t _index = 0;

private:
    future<> parse_fragment();
public:
    websocket_input_stream() = default;

    explicit websocket_input_stream(input_stream<char> stream) : _stream(std::move(stream)) {}

    websocket_input_stream(websocket_input_stream &&) = default;

    websocket_input_stream &operator=(websocket_input_stream &&) = default;

    future<std::unique_ptr<httpd::websocket_message>> read();

    future<> read_fragment();

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