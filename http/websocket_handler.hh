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

#include "handlers.hh"

namespace seastar {
namespace httpd {
namespace websocket {

/**
 * The most basic handler possible. Your function is called when a new connection arrives, passing the
 * connected_websocket.
 * Note that, when using this handler, you need to properly support PING/PONG.
 * @tparam type whether the underlying connected_websocket is a SERVER or CLIENT one
 */
template<websocket::endpoint_type type>
class ws_function_handler : public httpd::handler_websocket_base {
    typedef std::function<future<>(std::unique_ptr<request>,
            websocket::connected_websocket<type>&)> future_ws_handler_function;
public:
    ws_function_handler(const future_ws_handler_function& f_handle)
            : _f_handle(f_handle) {
    }

    future<> handle(const sstring& path, connected_websocket<type>& ws, std::unique_ptr<request> request) override {
        return _f_handle(std::move(request), ws);
    }

protected:
    future_ws_handler_function _f_handle;
};

/**
 * Standard websocket handler, enables "event" programming.
 * It provides automatic support for responding to PING message, but you can "override" this behavior.
 * @tparam type whether the underlying connected_websocket is a SERVER or CLIENT one
 */
template<websocket::endpoint_type type>
class ws_handler : public httpd::handler_websocket_base {
    typedef std::function<future<>(const std::unique_ptr<request>&)> future_ws_on_disconnected;
    typedef std::function<future<>(const std::unique_ptr<request>&, duplex_stream<type>&)> future_ws_on_connected;
    typedef std::function<future<>(const std::unique_ptr<request>&, duplex_stream<type>&,
            message<type>)> future_ws_on_message;

    typedef std::function<void(const std::unique_ptr<request>&)> void_ws_on_disconnected;
    typedef std::function<void(const std::unique_ptr<request>&)> void_ws_on_connected;
    typedef std::function<void(const std::unique_ptr<request>&, message<type> message)> void_ws_on_message;

public:
    ws_handler() : _on_connection(
            [](const std::unique_ptr<request>&, duplex_stream<type>&) { return make_ready_future(); }),
            _on_message([](const std::unique_ptr<request>&, duplex_stream<type>&,
                    message<type> message) { return make_ready_future(); }),
            _on_disconnection([](const std::unique_ptr<request>&) { return make_ready_future(); }),
            _on_pong([](const std::unique_ptr<request>&, duplex_stream<type>&,
                    message<type> message) { return make_ready_future(); }),
            _on_ping([](const std::unique_ptr<request>&, duplex_stream<type>& stream, message<type> message) {
                message.opcode = opcode::PONG;
                return stream.write(std::move(message)).then([&stream] {
                    return stream.flush();
                });
            })
    { }

    future<> handle(const sstring& path, connected_websocket<type>& ws, std::unique_ptr<request> req) override {
        return do_with(ws.stream(), std::move(req),
                [this](duplex_stream<type>& stream, std::unique_ptr<request>& req) {
                    return _on_connection(req, stream).then([this, &stream, &req] {
                        return repeat([this, &stream, &req] () -> future<stop_iteration> {
                            return stream.read().then([this, &req, &stream](std::experimental::optional<message<type>> message) -> future<stop_iteration> {
                                if (!message) {
                                    return make_ready_future<stop_iteration>(stop_iteration::yes);
                                }
                                return on_message_internal(req, stream, *message).then([] {
                                    return stop_iteration::no;
                                });
                            });
                        });
                    }).finally([this, &req] {
                        _on_disconnection(req);
                    });
                });
    }

    /**
     * Sets the handler for TEXT/BINARY messages.
     * This should only be used for non-blocking handler.
     * @param handler function that is to be called
     */
    void on_message(const void_ws_on_message& handler) {
        _on_message = [handler](const std::unique_ptr<request>& req, duplex_stream<type>&, message<type> message) {
            handler(req, std::move(message));
            return make_ready_future();
        };
    }

    /**
     * Sets the handler for PING messages.
     * This should only be used for non-blocking handler.
     * @param handler function that is to be called
     */
    void on_ping(const void_ws_on_message& handler) {
        _on_ping = [handler](const std::unique_ptr<request>& req, duplex_stream<type>&, message<type> message) {
            handler(req, std::move(message));
            return make_ready_future();
        };
    }

    /**
     * Sets the handler for PONG messages.
     * This should only be used for non-blocking handler.
     * @param handler function that is to be called
     */
    void on_pong(const void_ws_on_message& handler) {
        _on_pong = [handler](const std::unique_ptr<request>& req, duplex_stream<type>&, message<type> message) {
            handler(req, std::move(message));
            return make_ready_future();
        };
    }

    /**
     * Sets the handler for new connections.
     * This should only be used for non-blocking handler.
     * @param handler function that is to be called
     */
    void on_connection(const void_ws_on_connected& handler) {
        _on_connection = [handler](const std::unique_ptr<request>& req, duplex_stream<type>&) {
            handler(req);
            return make_ready_future();
        };
    }

    /**
     * Sets the handler for closed connections.
     * This should only be used for non-blocking handler.
     * @param handler function that is to be called
     */
    void on_disconnection(const void_ws_on_disconnected& handler) {
        _on_disconnection = [handler](const std::unique_ptr<request>& req) {
            handler(req);
            return make_ready_future();
        };
    }

    /**
     * Sets the handler for TEXT/BINARY messages.
     * @param handler the future that is to be called
     */
    void on_message_future(const future_ws_on_message& handler) { _on_message = handler; }

    /**
     * Sets the handler for PING messages.
     * @param handler the future that is to be called
     */
    void on_ping_future(const future_ws_on_message& handler) { _on_ping = handler; }

    /**
     * Sets the handler for PONG messages.
     * @param handler the future that is to be called
     */
    void on_pong_future(const future_ws_on_message& handler) { _on_pong = handler; }

    /**
     * Sets the handler for new connections.
     * @param handler the future that is to be called
     */
    void on_connection_future(const future_ws_on_connected& handler) { _on_connection = handler; }

    /**
     * Sets the handler for closed connections.
     * @param handler the future that is to be called
     */
    void on_disconnection_future(const future_ws_on_disconnected& handler) { _on_disconnection = handler; }

private:

    future<> on_message_internal(const std::unique_ptr<request>& req, duplex_stream<type>& stream,
            message<type>& message) {
        switch (message.opcode) {
            case opcode::TEXT:
            case opcode::BINARY:
                return _on_message(req, stream, std::move(message));
            case opcode::PING:
                return _on_ping(req, stream, std::move(message));
            case opcode::PONG:
                return _on_pong(req, stream, std::move(message));
            default: //Other opcode are handled at a lower, protocol level.
                return stream.close(close_status_code::UNEXPECTED_CONDITION); //Hum... This is embarrassing
        }
    }

protected:
    future_ws_on_connected _on_connection;
    future_ws_on_message _on_message;
    future_ws_on_disconnected _on_disconnection;
    future_ws_on_message _on_pong;
    future_ws_on_message _on_ping;
};

}
}
}