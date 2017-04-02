//
// Created by hippolyteb on 3/9/17.
//

#include "websocket.hh"

httpd::connected_websocket::connected_websocket(connected_socket *socket,
                                         socket_address &remote_adress) noexcept : _socket(std::move(socket)),
                                                                                   remote_adress(remote_adress) {}

httpd::connected_websocket::connected_websocket(httpd::connected_websocket &&cs) noexcept : _socket(cs._socket),
                                                                                            remote_adress(cs.remote_adress) {
}

httpd::connected_websocket &httpd::connected_websocket::operator=(httpd::connected_websocket &&cs) noexcept {
    _socket = std::move(cs._socket);
    remote_adress = std::move(cs.remote_adress);
    return *this;
}

future<httpd::inbound_websocket_fragment> httpd::websocket_input_stream::readFragment() {
    return _stream.read().then([] (temporary_buffer<char> buf) {
        inbound_websocket_fragment fragment(std::move(buf));
        return std::move(fragment);
    });
}

future<temporary_buffer<char>> httpd::websocket_input_stream::read() {
    if (!_buf.empty())
        _buf.reset();
    return repeat([this] { // gather all fragments and concatenate full message
        return readFragment().then([this] (inbound_websocket_fragment fragment) {
            if (fragment.fin()) {
                if (_buf.empty()) {
                    _lastmassage = std::move(fragment.message);
                    return stop_iteration::yes;
                }
                else {
                    _buf.append(fragment.message.begin(), fragment.message.size());
                    _lastmassage = std::move(temporary_buffer<char>(_buf.begin(), _buf.size(), fragment.message.release()));
                }
            }
            else
                _buf.append(fragment.message.begin(), fragment.message.size());
            return stop_iteration::no;
        });
    }).then([this] { return std::move(_lastmassage); });
}

future<> httpd::websocket_output_stream::write(websocket_opcode kind, temporary_buffer<char> buf) {
    outbound_websocket_fragment fragment(kind, std::move(buf));
    return do_with(std::move(fragment), [this] (outbound_websocket_fragment &frag) {
        return this->_stream.write(std::move(frag.get_header()))
                .then([this, &frag] { return this->_stream.write(std::move(frag.message)); })
                .then([this] { return this->_stream.flush(); });
    });
}

/*future<> httpd::websocket_output_stream::flush() {
    return this->_stream.flush();
}*/
