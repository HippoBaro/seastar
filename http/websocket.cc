//
// Created by hippolyteb on 3/9/17.
//

#include "websocket.hh"

httpd::connected_websocket::connected_websocket(connected_socket *socket, socket_address &remote_adress,
                                                request &request) noexcept : _socket(std::move(socket)),
                                                                             remote_adress(remote_adress),
                                                                             _request(request) {}

httpd::connected_websocket::connected_websocket(httpd::connected_websocket &&cs) noexcept : _socket(std::move(cs._socket)),
                                                                                            remote_adress(cs.remote_adress),
                                                                                            _request(std::move(cs._request)) {
}

httpd::connected_websocket &httpd::connected_websocket::operator=(httpd::connected_websocket &&cs) noexcept {
    _socket = std::move(cs._socket);
    remote_adress = std::move(cs.remote_adress);
    return *this;
}

future<httpd::inbound_websocket_fragment> httpd::websocket_input_stream::read_fragment() {
    return _stream.read().then([] (temporary_buffer<char> buf) {
        if (!buf)
            return inbound_websocket_fragment();
        inbound_websocket_fragment fragment(std::move(buf));
        return std::move(fragment);
    });
}

future<httpd::websocket_message> httpd::websocket_input_stream::read() {
    _lastmassage = websocket_message();
    return repeat([this] { // gather all fragments and concatenate full message
        return read_fragment().then([this] (inbound_websocket_fragment fragment) {
            if (!fragment)
                return stop_iteration::yes;
            else if (fragment.fin()) {
                if (_lastmassage.empty())
                    _lastmassage = std::move(websocket_message(std::move(fragment)));
                else
                    _lastmassage.append(std::move(fragment));
                return stop_iteration::yes;
            }
            _lastmassage.append(std::move(fragment));
            return stop_iteration::no;
        });
    }).then([this] {
        _lastmassage.done();
        return std::move(_lastmassage);
    });
}

future<> httpd::websocket_output_stream::write(websocket_message message) {
    outbound_websocket_fragment frag(std::move(message.to_fragment()));
    return do_with(std::move(frag), [this] (outbound_websocket_fragment &frag) {
        return this->_stream.write(std::move(frag.get_header()))
                .then([this, &frag] { return this->_stream.write(std::move(frag.message)); })
                .then([this] { return this->_stream.flush(); });
    });
}

future<> httpd::websocket_output_stream::write(websocket_opcode kind, temporary_buffer<char> buf) {
    websocket_message message(kind, std::move(buf));
    return write(std::move(message));
}