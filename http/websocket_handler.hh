//
// Created by hbarraud on 4/1/17.
//

#ifndef SEASTAR_WEBSOCKET_HANDLER_HH
#define SEASTAR_WEBSOCKET_HANDLER_HH

#include "handlers.hh"

namespace httpd {

typedef std::function<future<>(std::unique_ptr<request> req, connected_websocket ws)> future_ws_handler_function;

class websocket_function_handler : public httpd::handler_websocket_base {

public:
    websocket_function_handler(const future_ws_handler_function & f_handle)
            : _f_handle(f_handle) {
    }

    future<> handle(const sstring &path, connected_websocket ws, std::unique_ptr<request> request) override {
        return _f_handle(std::move(request), std::move(ws));
    }

protected:
    future_ws_handler_function _f_handle;
};

typedef std::function<future<>(const std::unique_ptr<request>&, websocket_output_stream* ws)> future_ws_on_dis_connected;
typedef std::function<future<>(const std::unique_ptr<request>&, websocket_output_stream* ws, httpd::websocket_message message)> future_ws_on_message;

typedef std::function<void(const std::unique_ptr<request>&, websocket_output_stream* ws)> void_ws_on_dis_connected;
typedef std::function<void(const std::unique_ptr<request>&, websocket_output_stream* ws, httpd::websocket_message message)> void_ws_on_message;

class websocket_handler : public httpd::handler_websocket_base {

public:
    websocket_handler() : _on_connection([] (const std::unique_ptr<request>&, websocket_output_stream* ws) { return make_ready_future(); }),
                          _on_message([] (const std::unique_ptr<request>&, websocket_output_stream* ws, httpd::websocket_message message) { return make_ready_future(); }),
                          _on_disconnection([] (const std::unique_ptr<request>&, websocket_output_stream* ws) { return make_ready_future(); }),
                          _on_pong([] (const std::unique_ptr<request>&, websocket_output_stream* ws, httpd::websocket_message message) { return make_ready_future(); }),
                          _on_ping([] (const std::unique_ptr<request>&, websocket_output_stream* ws, httpd::websocket_message message) {
                              message.opcode = websocket_opcode::PONG;
                              return ws->write(std::move(message));
                          })  {}

    future<> handle(const sstring &path, connected_websocket ws, std::unique_ptr<request> req) override {
        auto input = ws.input();
        auto output = ws.output();
        return do_with(std::move(ws), std::move(input), std::move(output), std::move(req),
                       [this] (connected_websocket &ws, websocket_input_stream &input, websocket_output_stream &output,
                               std::unique_ptr<request>& req) {
            return _on_connection(req, &output).then([this, &ws, &input, &output, &req] {
                return repeat([this, &input, &output, &ws, &req] {
                    return input.read().then_wrapped([this, &output, &ws, &req](future<httpd::websocket_message> f) {
                        if (f.failed())
                            return make_ready_future<bool_class<stop_iteration_tag>>(stop_iteration::yes);
                        auto buf = std::get<0>(f.get());
                        if (!buf)
                            return make_ready_future<bool_class<stop_iteration_tag>>(stop_iteration::yes);
                        return on_message_internal(req, &output, std::move(buf)).then([] (bool close) {
                            return bool_class<stop_iteration_tag>(close);
                        });
                    });
                });
            }).then([this, &ws, &output, &req] { return _on_disconnection(req, &output); });
        });
    }

    void on_message(const void_ws_on_message & handler) {
        _on_message = [handler](const std::unique_ptr<request>& req, websocket_output_stream *ws,
                                httpd::websocket_message message) {
            handler(req, ws, std::move(message));
            return make_ready_future();
        };
    }
    void on_ping(const void_ws_on_message & handler) {
        _on_ping = [handler](const std::unique_ptr<request>& req, websocket_output_stream *ws,
                             httpd::websocket_message message) {
            handler(req, ws, std::move(message));
            return make_ready_future();
        };
    }
    void on_pong(const void_ws_on_message & handler) {
        _on_pong = [handler](const std::unique_ptr<request>& req, websocket_output_stream *ws,
                             httpd::websocket_message message) {
            handler(req, ws, std::move(message));
            return make_ready_future();
        };
    }
    void on_connection(const void_ws_on_dis_connected & handler) {
        _on_connection = [handler](const std::unique_ptr<request>& req, websocket_output_stream *ws) {
            handler(req, ws);
            return make_ready_future();
        };
    }
    void on_disconnection(const void_ws_on_dis_connected & handler) {
        _on_disconnection = [handler](const std::unique_ptr<request>& req, websocket_output_stream *ws) {
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

    future<bool> on_message_internal(const std::unique_ptr<request>& req, websocket_output_stream* ws,
                                     httpd::websocket_message message) {
        switch (message.opcode){
            case TEXT:
                //FIXME Check that buffer is valid UTF-8
            case BINARY:
                return _on_message(req, ws, std::move(message)).then([] {
                    return false;
                });
            case PING:
                return _on_ping(req, ws, std::move(message)).then([] {
                    return false;
                });
            case PONG:
                return _on_pong(req, ws, std::move(message)).then([] {
                    return false;
                });
            case CLOSE:
            case RESERVED:
            default:
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
