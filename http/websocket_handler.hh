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

    future<> handle(const sstring &path, connected_websocket ws) override {
        return _f_handle(std::move(ws._request), std::move(ws));
    }

protected:
    future_ws_handler_function _f_handle;
};

typedef std::function<future<>(std::unique_ptr<request>* req, websocket_output_stream* ws)> future_ws_on_dis_connected;
typedef std::function<future<>(std::unique_ptr<request>* req, websocket_output_stream* ws, temporary_buffer<char> message)> future_ws_on_message;

class websocket_handler : public httpd::handler_websocket_base {

public:
    websocket_handler() : _on_connection([] (std::unique_ptr<request>*, websocket_output_stream* ws) { return make_ready_future(); }),
                          _on_message([] (std::unique_ptr<request>*, websocket_output_stream* ws, temporary_buffer<char> message) { return make_ready_future(); }),
                          _on_disconnection([] (std::unique_ptr<request>*, websocket_output_stream* ws) { return make_ready_future(); })  {}

    future<> handle(const sstring &path, connected_websocket ws) override {
        auto input = ws.input();
        auto output = ws.output();
        return do_with(std::move(ws), std::move(input), std::move(output), [this] (connected_websocket &ws,
                                                                                   websocket_input_stream &input,
                                                                                   websocket_output_stream &output) {
            return _on_connection(&ws._request, &output).then([this, &ws, &input, &output] {
                return repeat([this, &input, &output, &ws] {
                    return input.read().then([this, &output, &ws](temporary_buffer<char> buf){
                        if (!buf)
                            return make_ready_future<bool_class<stop_iteration_tag>>(stop_iteration::yes);
                        return _on_message(&ws._request, &output, std::move(buf)).then([] {
                            return stop_iteration::no;
                        });
                    });
                });
            });
        }).then([this, &ws, &output] { return _on_disconnection(&ws._request, &output); });
    }

    void on_message(const future_ws_on_message & handler) { _on_message = future_ws_on_message(handler); }
    void on_connection(const future_ws_on_dis_connected & handler) { _on_connection = future_ws_on_dis_connected(handler); }
    void on_disconnection(const future_ws_on_dis_connected & handler) { _on_disconnection = future_ws_on_dis_connected(handler); }

protected:
    future_ws_on_dis_connected _on_connection;
    future_ws_on_message _on_message;
    future_ws_on_dis_connected _on_disconnection;
};

}


#endif //SEASTAR_WEBSOCKET_HANDLER_HH
