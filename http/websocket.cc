//
// Created by hippolyteb on 3/9/17.
//

#include "websocket.hh"

httpd::connected_websocket::connected_websocket(connected_socket *socket, socket_address &remote_adress,
                                                request &request) noexcept : _socket(std::move(socket)),
                                                                             remote_adress(remote_adress),
                                                                             _request(request) {
    //std::cout << "new websocket on core #" << engine().cpu_id() << std::endl;
}

httpd::connected_websocket::connected_websocket(httpd::connected_websocket &&cs) noexcept : _socket(std::move(cs._socket)),
                                                                                            remote_adress(cs.remote_adress),
                                                                                            _request(std::move(cs._request)) {
}

httpd::connected_websocket &httpd::connected_websocket::operator=(httpd::connected_websocket &&cs) noexcept {
    _socket = std::move(cs._socket);
    remote_adress = std::move(cs.remote_adress);
    return *this;
}

future<httpd::inbound_websocket_fragment> httpd::websocket_input_stream::parse_fragment() {
    if (_buf.size() - _index < 3)
        return make_ready_future<httpd::inbound_websocket_fragment>(std::move(inbound_websocket_fragment()));
    inbound_websocket_fragment fragment(_buf, &_index);
    return make_ready_future<httpd::inbound_websocket_fragment>(std::move(fragment));
}

future<httpd::inbound_websocket_fragment> httpd::websocket_input_stream::read_fragment() {
    if (!_buf || _index == _buf.size()) {
        return _stream.read().then([this] (temporary_buffer<char> buf) {
            _buf = std::move(buf);
            _index = 0;
        }).then([this] {
            return parse_fragment();
        });
    }
    return parse_fragment();
}

future<httpd::websocket_message> httpd::websocket_input_stream::read() {
    _lastmassage = std::move(websocket_message());
    return repeat([this] { // gather all fragments and concatenate full message
        return read_fragment().then([this] (inbound_websocket_fragment fragment) {
            if (!fragment)
            {
                _lastmassage = std::move(websocket_message());
                return stop_iteration::yes;
            }
            else if (fragment.fin()) {
                //std::cout << "received fin fragment" << std::endl;
                if (_lastmassage.empty())
                    _lastmassage = std::move(websocket_message(std::move(fragment)));
                else {
                    _lastmassage.append(std::move(fragment));
                }
                return stop_iteration::yes;
            }
            else if (fragment.opcode() == CONTINUATION) {
                //std::cout << "received continuation fragment" << std::endl;
                _lastmassage.append(std::move(fragment));
            }
            return stop_iteration::no;
        });
    }).then([this] {
        return std::move(_lastmassage);
    });
}

future<> httpd::websocket_output_stream::write(websocket_message message) {
    message.done();
    return do_with(std::move(message), [this] (websocket_message &frag) {
        return this->_stream.write(std::move(frag._header)).then([this, &frag] {
            return do_for_each(frag._fragments, [this] (temporary_buffer<char> &buff) {
                return this->_stream.write(std::move(buff));
            }).then([this] {
                return this->_stream.flush();
            });
        });
    });
}

future<> httpd::websocket_output_stream::write(websocket_opcode kind, temporary_buffer<char> buf) {
    websocket_message message(kind, std::move(buf));
    return write(std::move(message));
}