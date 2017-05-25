/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * Copyright 2015 Cloudius Systems
 */

#pragma once

#include <core/reactor.hh>
#include "request.hh"
#include "websocket_fragment.hh"
#include "websocket_message.hh"

namespace seastar {
namespace httpd {
namespace websocket {

class output_stream_base {
private:
    seastar::output_stream<char> _stream;
public:

    output_stream_base() noexcept = delete;

    output_stream_base(seastar::output_stream<char>&& stream) noexcept: _stream(std::move(stream)) {}

    output_stream_base(output_stream_base&&) noexcept = default;

    output_stream_base& operator=(output_stream_base&&) noexcept = default;

    future<> close() { return _stream.close(); };

    future<> flush() { return _stream.flush(); };

protected:
    future<> write(temporary_buffer<char> header, message_base message) {
        return _stream.write(std::move(header)).then([this, message = std::move(message)]() mutable -> future<> {
            return _stream.write(std::move(message.payload));
        });
    }

    friend class reactor;
};

template<websocket::endpoint_type type>
class output_stream final : public output_stream_base {
    using output_stream_base::output_stream_base;
public:
    future<> write(websocket::message<type> message) {
        auto header = message.get_header();
        return output_stream_base::write(std::move(header), std::move(message));
    };
};

class input_stream_base {
protected:
    seastar::input_stream<char> _stream;

public:
    input_stream_base() noexcept = delete;

    input_stream_base(seastar::input_stream<char>&& stream) noexcept: _stream(std::move(stream)) {}

    input_stream_base(input_stream_base&&) noexcept = default;

    input_stream_base& operator=(input_stream_base&&) noexcept = default;

    future<> close() { return _stream.close(); }
};

template<websocket::endpoint_type type>
class input_stream final : public input_stream_base {
    using input_stream_base::input_stream_base;
private:
    std::vector<inbound_fragment<type>> _fragmented_message;
    websocket::message<type> _message;

public:
    /*
     * Websocket fragments can be sent in chop, using read_exactly leverage seastar network stack to make an exception
     * the general case. So, we read_exactly(2) to get the next frame header and from there we read accordingly.
     * As the RFC states, websocket frames are atomic/indivisible. They cannot be sent as multiple packets, so when the
     * first read_exactly() returns, the next ones will return immediately.
     */
    future<inbound_fragment<type>> read_fragment() {
        return _stream.read_exactly(sizeof(uint16_t)).then([this](temporary_buffer<char>&& header) {
            if (!header)
                throw websocket_exception(NORMAL_CLOSURE); //EOF
            fragment_header fragment_header(header);
            if (fragment_header.extended_header_size() > 0) {
                return _stream.read_exactly(fragment_header.extended_header_size()).then(
                        [this, fragment_header](temporary_buffer<char> extended_header) mutable {
                            if (!extended_header)
                                throw websocket_exception(NORMAL_CLOSURE); //EOF
                            fragment_header.feed_extended_header(extended_header);
                            return _stream.read_exactly(fragment_header.length).then(
                                    [this, fragment_header](temporary_buffer<char>&& payload) {
                                        if (!payload && fragment_header.length > 0)
                                            throw websocket_exception(NORMAL_CLOSURE); //EOF
                                        return inbound_fragment<type>(fragment_header, payload);
                                    });
                        });
            }
            return _stream.read_exactly(fragment_header.length).then(
                    [this, fragment_header](temporary_buffer<char>&& payload) {
                        if (!payload && fragment_header.length > 0)
                            throw websocket_exception(NORMAL_CLOSURE); //EOF
                        return inbound_fragment<type>(fragment_header, payload);
                    });
        });
    }

    /*
     * Because empty websocket frames/messages are semantically valid, we cannot reproduce seastar standard behavior
     * with dealing with EOF (testing operator bool()). So, we use exception instead to signal errors/eof.
     */
    future<websocket::message<type>> read() {
        return repeat([this] { // gather all fragments
            return read_fragment().then([this](inbound_fragment<type>&& fragment) {
                if (!fragment) { throw websocket_exception(PROTOCOL_ERROR); }
                switch (fragment.header.opcode) {
                    case websocket::CONTINUATION: {
                        if (!_fragmented_message.empty()) { _fragmented_message.emplace_back(std::move(fragment)); }
                        else { throw websocket_exception(PROTOCOL_ERROR); } //protocol error, close connection
                        if (fragment.header.fin) {
                            _message = websocket::message<type>(_fragmented_message);
                            _fragmented_message.clear();
                        }
                        return stop_iteration(fragment.header.fin);
                    }

                    case TEXT:
                    case BINARY: {
                        if (fragment.header.fin && _fragmented_message.empty()) {
                            _message = websocket::message<type>(fragment);
                        } else if (!fragment.header.fin && _fragmented_message.empty()) {
                            _fragmented_message.emplace_back(std::move(fragment));
                        } else { throw websocket_exception(PROTOCOL_ERROR); } //protocol error, close connection
                        return stop_iteration(fragment.header.fin);
                    }

                    case PING:
                    case PONG: {
                        if (fragment.header.fin) { _message = websocket::message<type>(fragment); }
                        else { throw websocket_exception(PROTOCOL_ERROR); } //protocol error, close connection
                        return stop_iteration::yes;
                    }

                    case CLOSE: //remote pair asked for close
                        throw websocket_exception(NONE); //protocol error, close connection

                    case RESERVED:
                    default:
                        throw websocket_exception(UNEXPECTED_CONDITION); //"Hum.. this is embarrassing"
                }
            });
        }).then([this] {
            return std::move(_message);
        });
    }
};

/*
 * The websocket protocol specifies that, when closing a websocket connection, a CLOSE frame must be sent.
 * Hence the need for a duplex stream able to send messages if a CLOSE frame is received, or if errors occurred
 * when reading from the underlying socket_stream.
 */
template<websocket::endpoint_type type>
class duplex_stream {
private:
    input_stream<type> _input_stream;
    output_stream<type> _output_stream;

public:
    duplex_stream(input_stream<type>&& input_stream,
            output_stream<type>&& output_stream) noexcept:
            _input_stream(std::move(input_stream)),
            _output_stream(std::move(output_stream)) {}

    duplex_stream(duplex_stream&&) noexcept = default;

    duplex_stream& operator=(duplex_stream&&) noexcept = default;

    future<websocket::message<type>> read() {
        return _input_stream.read().handle_exception_type([this] (websocket_exception& ex) {
            return close(ex.status_code).then([ex = std::move(ex)]() -> future<websocket::message<type>> {
                return make_exception_future<websocket::message<type>>(ex);
            });
        }).then([](websocket::message<type> message) {
            return make_ready_future<websocket::message<type>>(std::move(message));
        });
    }

    future<> write(websocket::message<type> message) {
        return _output_stream.write(std::move(message));
    };

    future<> close(close_status_code code = NORMAL_CLOSURE) {
        return write(websocket::make_close_message<type>(code)).then([this] {
            return _output_stream.flush();
        }).finally([this] {
            return when_all(_input_stream.close(), _output_stream.close()).discard_result();
        });
    };

    future<> flush() { return _output_stream.flush(); };
};

template<websocket::endpoint_type type>
class connected_websocket {
private:
    seastar::connected_socket _socket;

public:
    socket_address remote_adress;

    connected_websocket(connected_socket socket, const socket_address remote_adress) noexcept :
            _socket(std::move(socket)), remote_adress(remote_adress) {
    }

    connected_websocket(connected_websocket&& cs) noexcept : _socket(
            std::move(cs._socket)), remote_adress(cs.remote_adress) {
    }

    connected_websocket& operator=(connected_websocket&& cs) noexcept {
        _socket = std::move(cs._socket);
        remote_adress = std::move(cs.remote_adress);
        return *this;
    };

    duplex_stream<type> stream() {
        return duplex_stream<type>(websocket::input_stream<type>(std::move(_socket.input())),
                websocket::output_stream<type>(std::move(_socket.output())));
    }

    void shutdown_output() { _socket.shutdown_output(); }

    void shutdown_input() { _socket.shutdown_input(); }
};

sstring encode_handshake_key(sstring nonce);

/**
 * Connect a websocket. Can throw if unable to reach and/or establish the connection to the remote host
 * @param sa remote host to connect to
 * @param local local address
 * @return a connected_websocket
 */
future<connected_websocket<CLIENT>>
connect(socket_address sa, socket_address local = socket_address(::sockaddr_in{AF_INET, INADDR_ANY, {0}}));

}
}
}