//
// Created by hippolyteb on 3/9/17.
//

#include "websocket.hh"

httpd::connected_websocket::connected_websocket(connected_socket *socket, socket_address &remote_adress,
                                                request &request) noexcept : _socket(std::move(socket)),
                                                                             remote_adress(remote_adress),
                                                                             _request(request) {
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

future<> httpd::websocket_input_stream::read_fragment() {
    auto parse_fragment = [this] {
        if (_buf.size() - _index > 2)
            _fragment = std::move(std::make_unique<inbound_websocket_fragment>(_buf, &_index));
    };

    _fragment = nullptr;
    if (!_buf || _index >= _buf.size())
        return _stream.read().then([this, parse_fragment](temporary_buffer<char> buf) {
            _buf = std::move(buf);
            _index = 0;
            parse_fragment();
        });
    parse_fragment();
    return make_ready_future();
}

future<std::unique_ptr<httpd::websocket_message>> httpd::websocket_input_stream::read() {
    _lastmassage = nullptr;
    return repeat([this] { // gather all fragments and concatenate full message
        return read_fragment().then([this] {
            if (!_fragment || _fragment->_is_empty)
                return stop_iteration::yes;
            else if (_fragment->fin()) {
                if (!_lastmassage)
                    _lastmassage = std::move(std::make_unique<websocket_message>(std::move(_fragment)));
                else
                    _lastmassage->append(std::move(_fragment));
                return stop_iteration::yes;
            }
            else if (_fragment->opcode() == CONTINUATION)
                _lastmassage->append(std::move(_fragment));
            return stop_iteration::no;
        });
    }).then([this] {
        return std::move(_lastmassage);
    });
}

/*
 * When the write is called and (!_buf || _index >= _buf.size()) == false, it would make sense
 * to buff it and flush everything at once before the next read().
 */
future<> httpd::websocket_output_stream::write(std::unique_ptr<httpd::websocket_message> message) {
    message->done();
    return do_with(std::move(message), [this] (std::unique_ptr<httpd::websocket_message> &frag) {
        temporary_buffer<char> head((char *)&frag->_header, frag->_header_size); //FIXME copy memory to avoid mixed writes
        return this->_stream.write(std::move(head)).then([this, &frag] {
            return do_for_each(frag->_fragments, [this] (temporary_buffer<char> &buff) {
                return this->_stream.write(std::move(buff));
            });
        });
    }).then([this] {
        return this->_stream.flush();
    }).handle_exception([this] (std::exception_ptr e) {
        return _stream.close();
    });
}

future<> httpd::websocket_output_stream::write(websocket_opcode kind, temporary_buffer<char> buf) {
    return write(std::move(std::make_unique<websocket_message>(kind, std::move(buf))));
}