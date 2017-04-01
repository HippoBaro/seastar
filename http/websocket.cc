//
// Created by hippolyteb on 3/9/17.
//

#include "websocket.hh"

server_websocket::server_websocket(socket_address sa, listen_options opts) : _sa(sa), _opts(opts) {}

void server_websocket::listen() {
    _server_socket = engine().listen(_sa, _opts);
}

future<connected_websocket> server_websocket::accept() {
    return _server_socket.accept().then([] (connected_socket sock, socket_address addr) {
        return connected_websocket(std::move(sock), addr);
    });
}

connected_websocket::connected_websocket(connected_socket &&socket,
                                         socket_address &remote_adress) noexcept : _socket(std::move(socket)), remote_adress(remote_adress) {}

connected_websocket::connected_websocket(connected_websocket &&cs) noexcept : _socket(std::move(cs._socket)), remote_adress(cs.remote_adress) {

}

connected_websocket &connected_websocket::operator=(connected_websocket &&cs) noexcept {
    _socket = std::move(cs._socket);
    remote_adress = std::move(cs.remote_adress);
    return *this;
}

future<websocket_fragment> websocket_input_stream::readFragment() {
    return _stream.read().then([] (temporary_buffer<char> buf) {
        websocket_fragment fragment(std::move(buf));
        return fragment;
    });
}

future<temporary_buffer<char>> websocket_input_stream::read() {
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

    }).then([this] {
        if (_buf.empty())
            return make_ready_future<temporary_buffer<char>>();
        std::cout<<"size is " << _buf.size() << std::endl;
        return make_ready_future<temporary_buffer<char>>(std::move(temporary_buffer<char>(_buf.c_str(), _buf.size())));
    });
}