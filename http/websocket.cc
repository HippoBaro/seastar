//
// Created by hippolyteb on 3/9/17.
//

#include "websocket.hh"

httpd::connected_websocket::connected_websocket(connected_socket *socket,
                                         socket_address &remote_adress, request &request) noexcept : _socket(std::move(socket)),
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
        inbound_websocket_fragment fragment(std::move(buf)); //FIXME possible nullref
        return std::move(fragment);
    });
}

future<httpd::websocket_message> httpd::websocket_input_stream::read_message() {
    return repeat([this] { // gather all fragments and concatenate full message
        return read_fragment().then([this] (inbound_websocket_fragment fragment) {
            if (!fragment) {
                _lastmassage = websocket_message();
                return stop_iteration::yes;
            }
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
    }).then([this] { return std::move(_lastmassage); });
}

future<temporary_buffer<char>> httpd::websocket_input_stream::read() {
    return read_message().then([this] (websocket_message message) {
        return std::move(message.payload());
    });
}

future<> httpd::websocket_output_stream::write(websocket_opcode kind, temporary_buffer<char> buf) {
    outbound_websocket_fragment fragment(kind, std::move(buf));
    return do_with(std::move(fragment), [this] (outbound_websocket_fragment &frag) {
        return this->_stream.write(std::move(frag.get_header()))
                .then([this, &frag] { return this->_stream.write(std::move(frag.message)); })
                .then([this] { return this->_stream.flush(); });
    });
}

future<> httpd::websocket_output_stream::write(websocket_opcode kind, sstring buf) {
    temporary_buffer<char> tempbuf(buf.begin(), buf.size()); // FIXME strcpy
    return write(kind, std::move(tempbuf));
}
