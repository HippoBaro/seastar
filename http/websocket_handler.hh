//
// Created by hbarraud on 4/1/17.
//

#ifndef SEASTAR_WEBSOCKET_HANDLER_HH
#define SEASTAR_WEBSOCKET_HANDLER_HH

#include "handlers.hh"

namespace httpd {

typedef std::function<future<>(std::unique_ptr<request> req, connected_websocket ws)> future_ws_handler_function;

class websocket_handler : public httpd::handler_websocket_base {

public:
    websocket_handler(const future_ws_handler_function & f_handle)
            : _f_handle(f_handle) {
    }

    future<> handle(const sstring &path, std::unique_ptr<httpd::request> req, connected_websocket ws) override {
        return _f_handle(std::move(req), std::move(ws));
    }

protected:
    future_ws_handler_function _f_handle;
};

}


#endif //SEASTAR_WEBSOCKET_HANDLER_HH
