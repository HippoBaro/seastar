//
// Created by hippolyteb on 3/9/17.
//

#include "websocket.hh"

httpd::connected_websocket::connected_websocket(connected_socket *socket,
                                         socket_address &remote_adress) noexcept : _socket(std::move(socket)), remote_adress(remote_adress) {}

httpd::connected_websocket::connected_websocket(httpd::connected_websocket &&cs) noexcept : _socket(cs._socket), remote_adress(cs.remote_adress) {

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
    return readFragment().then([this] (inbound_websocket_fragment fragment) {
        return std::move(fragment.message);
    });
/*
    _buf.clear();
    return repeat([this] {
        return readFragment().then([this] (auto fragment) {
            if (fragment.data){
                _buf.append(fragment.data.get(), fragment.data.size());
                if (fragment.fin())
                    return stop_iteration::yes;
                return stop_iteration::no;
            }
            return stop_iteration::yes;
        });
*/

/*        return _stream.read().then([this] (temporary_buffer<char> buf) {
            if (buf) {
                _buf.append(buf.get(), buf.size());
                if (buf.get()[0] == 'h')
                    return stop_iteration::yes;
                return stop_iteration::no;
            } else {
                return stop_iteration::yes;
            }
        });
*/

/*    }).then([this] {
        if (_buf.empty())
            return make_ready_future<temporary_buffer<char>>();
        std::cout<<"size is " << _buf.size() << std::endl;
        return make_ready_future<temporary_buffer<char>>(std::move(temporary_buffer<char>(_buf.c_str(), _buf.size())));
    });*/
}

future<temporary_buffer<char>> httpd::websocket_input_stream::readRaw() {
    return this->_stream.read();
}

future<> httpd::websocket_output_stream::write(temporary_buffer<char> buf) {
    outbound_websocket_fragment fragment(websocket_fragment_base::opcode::TEXT, std::move(buf));
    return do_with(std::move(fragment), [this] (outbound_websocket_fragment &frag) {
        return this->_stream.write(std::move(frag.get_header()))
                .then([this, &frag] { return this->_stream.write(std::move(frag.message)); })
                .then([this] { return this->_stream.flush(); });
    });

}

/*future<> httpd::websocket_output_stream::flush() {
    return this->_stream.flush();
}*/
