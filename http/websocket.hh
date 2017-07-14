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
 * Copyright 2017 Hippolyte Barraud
 */

#pragma once

#include <core/reactor.hh>
#include "request.hh"
#include "websocket_fragment.hh"
#include "websocket_message.hh"

namespace seastar {
namespace httpd {
namespace websocket {

template<websocket::endpoint_type type>
class output_stream final {
private:
    seastar::output_stream<char> _stream;
public:
    output_stream() noexcept = delete;

    output_stream(seastar::output_stream<char>&& stream) noexcept: _stream(std::move(stream)) {}

    output_stream(output_stream&&) noexcept = default;

    output_stream& operator=(output_stream&&) noexcept = default;

    future<> close() { return _stream.close(); };

    future<> flush() { return _stream.flush(); };

    future<> write(websocket::message<type> message) {
        auto header = message.get_header();
        return _stream.write(std::move(header)).then([this, payload = std::move(message.payload)]() mutable {
          return _stream.write(std::move(payload));
        });
    };
};

template<websocket::endpoint_type type>
class input_stream final {
    using opt_message = std::experimental::optional<websocket::message<type>>;
    using opt_fragment = std::experimental::optional<inbound_fragment>;
private:
    seastar::input_stream<char> _stream;
    ///Represent a fragmented message
    std::vector<inbound_fragment> _fragmented_message;
    fragment_header _fragment_header;
    opt_message _message;

public:
    input_stream() noexcept = delete;

    input_stream(seastar::input_stream<char>&& stream) noexcept: _stream(std::move(stream)) {}

    input_stream(input_stream&&) noexcept = default;

    input_stream& operator=(input_stream&&) noexcept = default;

    future<> close() { return _stream.close(); }

    /**
     * Websocket fragments can be sent in chops. Using read_exactly makes the exception the general case.
     * So, we read_exactly(2) to get the next frame header and from there we read accordingly.
     * As the RFC states, websocket frames are atomic/indivisible. They cannot be sent as multiple TCP frames, so when
     * the first read_exactly() returns, the next ones should return immediately because there are already buffered.
     */
    future<opt_fragment> read_fragment() {
        return _stream.read_exactly(sizeof(uint16_t)).then([this](temporary_buffer<char>&& header) {
          if (!header) {
              return make_ready_future<opt_fragment>(inbound_fragment()); //EOF
          }
          _fragment_header = fragment_header(header);
          auto extended_header_read = make_ready_future<>();
          if (_fragment_header.extended_header_size() > 0) {
              // The frame has an extended header (bigger payload size and/or a masking key)
              extended_header_read = _stream.read_exactly(_fragment_header.extended_header_size())
                      .then([this](temporary_buffer<char> extended_header) mutable {
                        if (!extended_header) {
                            throw websocket_exception(close_status_code::UNEXPECTED_CONDITION); //Broken promise
                        }
                        _fragment_header.feed_extended_header(extended_header);
                      });
          }
          return extended_header_read.then([this] {
            return _stream.read_exactly(_fragment_header.length)
                    .then([this](temporary_buffer<char>&& payload) {
                      // Because empty frames are OK, an empty buffer does not necessarily means EOF.
                      if (!payload && _fragment_header.length > 0)
                          throw websocket_exception(close_status_code::UNEXPECTED_CONDITION); //Broken promise
                      return opt_fragment(inbound_fragment(_fragment_header, payload));
                    });
          });
        });
    }

    /**
     * Because empty websocket frames/messages are semantically valid, we cannot reproduce seastar standard behavior
     * with dealing with EOF. So, we use exception instead to signal errors/eof.
     * An exception always lead to closing the connection, so it should not hurt performance.
     */
    future<opt_message> read() {
        return repeat([this] { // gather all fragments
          return read_fragment().then([this](opt_fragment&& fragment) {
            if (!fragment) { //EOF
                _message = {};
                return stop_iteration::yes;
            }
            if (!fragment->is_valid()) {
                throw websocket_exception(close_status_code::PROTOCOL_ERROR);
            }
            switch (fragment->header.opcode) {
            case opcode::CONTINUATION: {
                if (!_fragmented_message.empty()) {
                    _fragmented_message.emplace_back(std::move(*fragment));
                }
                else { //protocol error, close connection
                    throw websocket_exception(close_status_code::PROTOCOL_ERROR);
                }
                if (fragment->header.fin) {
                    _message = websocket::message<type>(_fragmented_message);
                    _fragmented_message.clear();
                }
                return stop_iteration(fragment->header.fin);
            }

            case opcode::TEXT:
            case opcode::BINARY: {
                if (fragment->header.fin && _fragmented_message.empty()) {
                    _message = websocket::message<type>(*fragment);
                } else if (!fragment->header.fin && _fragmented_message.empty()) {
                    _fragmented_message.emplace_back(std::move(*fragment));
                } else { //protocol error, close connection
                    throw websocket_exception(close_status_code::PROTOCOL_ERROR);
                }
                return stop_iteration(fragment->header.fin);
            }

            case opcode::PING:
            case opcode::PONG: {
                if (!fragment->header.fin) {
                    throw websocket_exception(close_status_code::PROTOCOL_ERROR); //protocol error, close connection
                }
                _message = websocket::message<type>(*fragment);
                return stop_iteration::yes;
            }

            case opcode::CLOSE: { //remote pair asked for close
                throw websocket_exception(close_status_code::NONE); //close connection
            }
            default: { //"Hum.. this is embarrassing"
                throw websocket_exception(close_status_code::UNEXPECTED_CONDITION);
            }
            }
          });
        }).then([this] {
          return std::move(_message);
        });
    }
};

/**
 * The websocket protocol specifies that, when closing a connection, a CLOSE frame must be sent. Hence the need for a
 * duplex stream able to send messages if a CLOSE frame is received, or if errors occurred when reading from the
 * underlying stream.
 */
template<websocket::endpoint_type type>
class duplex_stream {
    using opt_message = std::experimental::optional<websocket::message<type>>;
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

    future<opt_message> read() {
        return _input_stream.read().then([this] (opt_message message) -> future<opt_message> {
          if (!message) {
              return close().then([] { return opt_message(); });
          }
          return make_ready_future<opt_message>(std::move(*message));
        }).handle_exception_type([this] (websocket_exception& ex) {
          return close(ex.status_code).then([ex = std::move(ex)]() -> future<opt_message> {
            return make_exception_future<opt_message>(ex);
          });
        });
    }

    future<> write(websocket::message<type> message) {
        return _output_stream.write(std::move(message));
    };

    future<> close(close_status_code code = close_status_code::NORMAL_CLOSURE) {
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
 * Connect a websocket. Can throw if unable to reach and/or establish the connection to the remote host.
 * This function is provided for seawreck only, it has not been tested thoroughly.
 * @param sa remote host to connect to
 * @param local local address
 * @return a connected_socket that can then be wrapped inside a connected_websocket
 */
future<connected_socket>
connect(socket_address sa, socket_address local = socket_address(::sockaddr_in{AF_INET, INADDR_ANY, {0}}));

}
}
}