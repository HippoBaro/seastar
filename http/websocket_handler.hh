//
// Created by hbarraud on 4/1/17.
//

#pragma once

#include "handlers.hh"

namespace seastar {
namespace httpd {
namespace websocket {

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
            }) {

    }

    future<> handle(const sstring& path, connected_websocket<type>& ws, std::unique_ptr<request> req) override {
        return do_with(ws.stream(), std::move(req),
                [this](duplex_stream<type>& stream, std::unique_ptr<request>& req) {
                    return _on_connection(req, stream).then([this, &stream, &req] {
                        return repeat([this, &stream, &req] {
                            return stream.read().then([this, &req, &stream](message<type> message) {
                                return on_message_internal(req, stream, message).then([] {
                                    return stop_iteration::no;
                                });
                            });
                        });
                    }).finally([this, &req] {
                        _on_disconnection(req);
                    });
                });
    }

    void on_message(const void_ws_on_message& handler) {
        _on_message = [handler](const std::unique_ptr<request>& req, duplex_stream<type>&, message<type> message) {
            handler(req, std::move(message));
            return make_ready_future();
        };
    }

    void on_ping(const void_ws_on_message& handler) {
        _on_ping = [handler](const std::unique_ptr<request>& req, duplex_stream<type>&, message<type> message) {
            handler(req, std::move(message));
            return make_ready_future();
        };
    }

    void on_pong(const void_ws_on_message& handler) {
        _on_pong = [handler](const std::unique_ptr<request>& req, duplex_stream<type>&, message<type> message) {
            handler(req, std::move(message));
            return make_ready_future();
        };
    }

    void on_connection(const void_ws_on_connected& handler) {
        _on_connection = [handler](const std::unique_ptr<request>& req, duplex_stream<type>&) {
            handler(req);
            return make_ready_future();
        };
    }

    void on_disconnection(const void_ws_on_disconnected& handler) {
        _on_disconnection = [handler](const std::unique_ptr<request>& req) {
            handler(req);
            return make_ready_future();
        };
    }

    void on_message_future(const future_ws_on_message& handler) { _on_message = handler; }

    void on_ping_future(const future_ws_on_message& handler) { _on_ping = handler; }

    void on_pong_future(const future_ws_on_message& handler) { _on_pong = handler; }

    void on_connection_future(const future_ws_on_connected& handler) { _on_connection = handler; }

    void on_disconnection_future(const future_ws_on_disconnected& handler) { _on_disconnection = handler; }

private:

    future<> on_message_internal(const std::unique_ptr<request>& req, duplex_stream<type>& stream,
            message<type>& message) {
        switch (message.opcode) {
            case TEXT:
            case BINARY:
                return _on_message(req, stream, std::move(message));
            case PING:
                return _on_ping(req, stream, std::move(message));
            case PONG:
                return _on_pong(req, stream, std::move(message));
            default: //Other opcode are handled at a lower, protocol level.
                throw std::exception();
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