//
// Created by hbarraud on 4/1/17.
//

#ifndef SEASTAR_WEBSOCKET_HANDLER_HH
#define SEASTAR_WEBSOCKET_HANDLER_HH

#include "handlers.hh"

namespace httpd {

typedef std::function<future<>(const httpd::request& req, connected_websocket ws)> future_ws_handler_function;

class websocket_function_handler : public httpd::handler_websocket_base {

public:
    websocket_function_handler(const future_ws_handler_function & f_handle)
            : _f_handle(f_handle) {
    }

    future<> handle(const sstring &path, connected_websocket ws) override {
        return _f_handle(ws._request, std::move(ws));
    }

protected:
    future_ws_handler_function _f_handle;
};

typedef std::function<future<>(const httpd::request&, websocket_output_stream* ws)> future_ws_on_dis_connected;
typedef std::function<future<>(const httpd::request&, websocket_output_stream* ws, websocket_message message)> future_ws_on_message;

typedef std::function<void(const httpd::request&, websocket_output_stream* ws)> void_ws_on_dis_connected;
typedef std::function<void(const httpd::request&, websocket_output_stream* ws, websocket_message message)> void_ws_on_message;

class websocket_handler : public httpd::handler_websocket_base {

public:
    websocket_handler() : _on_connection([] (const httpd::request&, websocket_output_stream* ws) { return make_ready_future(); }),
                          _on_message([] (const httpd::request&, websocket_output_stream* ws, websocket_message message) { return make_ready_future(); }),
                          _on_disconnection([] (const httpd::request&, websocket_output_stream* ws) { return make_ready_future(); }),
                          _on_pong([] (const httpd::request&, websocket_output_stream* ws, websocket_message message) { return make_ready_future(); }),
                          _on_ping([] (const httpd::request&, websocket_output_stream* ws, websocket_message message) {
                              message.opcode = websocket_opcode::PONG;
                              return ws->write(std::move(message));
                          })  {}

    future<> handle(const sstring &path, connected_websocket ws) override {
        auto input = ws.input();
        auto output = ws.output();
        return do_with(std::move(ws), std::move(input), std::move(output), [this] (connected_websocket &ws,
                                                                                   websocket_input_stream &input,
                                                                                   websocket_output_stream &output) {
            return _on_connection(ws._request, &output).then([this, &ws, &input, &output] {
                return repeat([this, &input, &output, &ws] {
                    return input.read().then([this, &output, &ws](websocket_message buf) {
                        if (buf.empty())
                            return make_ready_future<bool_class<stop_iteration_tag>>(stop_iteration::yes);
                        return on_message_internal(ws._request, &output, std::move(buf)).then([] (bool close) {
                            return bool_class<stop_iteration_tag>(close);
                        });
                    });
                });
            });
        }).then([this, &ws, &output] { return _on_disconnection(ws._request, &output); });
    }

    void on_message(const void_ws_on_message & handler) {
        _on_message = [handler](const httpd::request& req, websocket_output_stream *ws,
                                websocket_message message) {
            handler(req, ws, std::move(message));
            return make_ready_future();
        };
    }
    void on_ping(const void_ws_on_message & handler) {
        _on_ping = [handler](const httpd::request& req, websocket_output_stream *ws,
                                websocket_message message) {
            handler(req, ws, std::move(message));
            return make_ready_future();
        };
    }
    void on_pong(const void_ws_on_message & handler) {
        _on_pong = [handler](const httpd::request& req, websocket_output_stream *ws,
                             websocket_message message) {
            handler(req, ws, std::move(message));
            return make_ready_future();
        };
    }
    void on_connection(const void_ws_on_dis_connected & handler) {
        _on_connection = [handler](const httpd::request& req, websocket_output_stream *ws) {
            handler(req, ws);
            return make_ready_future();
        };
    }
    void on_disconnection(const void_ws_on_dis_connected & handler) {
        _on_disconnection = [handler](const httpd::request& req, websocket_output_stream *ws) {
            handler(req, ws);
            return make_ready_future();
        };
    }

    void on_message_future(const future_ws_on_message & handler) { _on_message = handler; }
    void on_ping_future(const future_ws_on_message & handler) { _on_ping = handler; }
    void on_pong_future(const future_ws_on_message & handler) { _on_pong = handler; }
    void on_connection_future(const future_ws_on_dis_connected & handler) { _on_connection = handler; }
    void on_disconnection_future(const future_ws_on_dis_connected & handler) { _on_disconnection = handler; }

private:

    future<bool> on_message_internal(const httpd::request& req, websocket_output_stream* ws, websocket_message message) {
        switch (message.opcode){
            case TEXT:
            case BINARY:
                std::cout << "BINARY/TEXT" << std::endl;
                return _on_message(req, ws, std::move(message)).then([] {
                    return false;
                });
            case PING:
                std::cout << "PING" << std::endl;
                return _on_ping(req, ws, std::move(message)).then([] {
                    return false;
                });
            case PONG:
                std::cout << "PONG" << std::endl;
                return _on_pong(req, ws, std::move(message)).then([] {
                    return false;
                });
            case CLOSE:
            case RESERVED:
            default:
                std::cout << "CLOSE" << std::endl;
                return make_ready_future().then([] {
                    return true;
                });
        }
    }

protected:
    future_ws_on_dis_connected _on_connection;
    future_ws_on_message _on_message;
    future_ws_on_dis_connected _on_disconnection;
    future_ws_on_message _on_pong;
    future_ws_on_message _on_ping;
};

}


#endif //SEASTAR_WEBSOCKET_HANDLER_HH
