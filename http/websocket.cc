//
// Created by hippolyteb on 3/9/17.
//

#include "websocket.hh"
#include "http/request_parser.hh"
#include <random>
#include <cryptopp/sha.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/base64.h>

httpd::connected_websocket::connected_websocket(connected_socket socket, const socket_address remote_adress,
                                                request request) noexcept : _socket(std::move(socket)),
                                                                            remote_adress(remote_adress),
                                                                            _request(request) {
}

httpd::connected_websocket::connected_websocket(httpd::connected_websocket &&cs) noexcept : _socket(
        std::move(cs._socket)), remote_adress(cs.remote_adress), _request(std::move(cs._request)) {
}

httpd::connected_websocket &httpd::connected_websocket::operator=(httpd::connected_websocket &&cs) noexcept {
    _socket = std::move(cs._socket);
    remote_adress = std::move(cs.remote_adress);
    return *this;
}

sstring
httpd::connected_websocket::generate_websocket_key(sstring nonce) {
    constexpr char websocket_uuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    constexpr size_t websocket_uuid_len = 36;

    CryptoPP::SHA hash;
    byte digest[CryptoPP::SHA::DIGESTSIZE];
    hash.Update((byte *) nonce.data(), nonce.size());
    hash.Update((byte *) websocket_uuid, websocket_uuid_len);
    hash.Final(digest);

    sstring base64;

    CryptoPP::Base64Encoder encoder;
    encoder.Put(digest, sizeof(digest));
    encoder.MessageEnd();
    CryptoPP::word64 size = encoder.MaxRetrievable();
    if (size) {
        base64.resize(size);
        encoder.Get((byte *) base64.data(), base64.size() -1);
    }

    return base64;
}

/*httpd::connected_websocket
httpd::connected_websocket::connect_websocket(socket_address sa, socket_address local) {
    return engine().net().connect(sa, local).then([local] (connected_socket fd) {
        input_stream<char> in = std::move(fd.input());
        output_stream<char> out = std::move(fd.output());
        http_request_parser parser;
        return do_with(std::move(fd), std::move(in), std::move(out), std::move(parser), [local](connected_socket &fd,
                                                                                                input_stream<char> &in,
                                                                                                output_stream<char> &out,
                                                                                                http_request_parser &parser) {

            using random_bytes_engine = std::independent_bits_engine<
                    std::default_random_engine, std::numeric_limits<unsigned char>::digits, unsigned char>;

            random_bytes_engine rbe;
            sstring key(16, '\0');
            std::generate(key.begin(), key.end(), std::ref(rbe));

            sstring nonce;
            CryptoPP::Base64Encoder encoder;
            encoder.Put((byte *) key.data(), key.size());
            encoder.MessageEnd();
            CryptoPP::word64 size = encoder.MaxRetrievable();
            if (size) {
                nonce.resize(size);
                encoder.Get((byte *) nonce.data(), nonce.size());
            } else {
                //fixme throw ?
            }

            std::stringstream stream;
            stream << "GET / HTTP/1.1\r\n"
                   << "Host: server.example.com\r\n"
                   << "Upgrade: websocket\r\n"
                   << "Connection: Upgrade\r\n"
                   << "Sec-WebSocket-Key: " << nonce << "\r\n"
                   << "Sec-WebSocket-Protocol: default\r\n"
                   << "Sec-WebSocket-Version: 13\r\n\r\n";

            return out.write(stream.str()).then([local, nonce, &out, &in, &parser, fd = std::move(fd)] {
                return out.flush();
            }).then([local, nonce, &in, &parser, fd = std::move(fd)] {
                parser.init();
                return in.consume(parser).then([local, nonce, &parser, fd = std::move(fd)](temporary_buffer<char> buf) {
                    if (parser.eof())
                        throw std::exception(); //FIXME : proper failure
                    std::unique_ptr<httpd::request> req = parser.get_parsed_request();
                    auto it = req->_headers.find("Sec-WebSocket-Accept");
                    if (it != req->_headers.end()
                        && it->second == httpd::connected_websocket::generate_websocket_key(nonce)) {
                        return std::move(httpd::connected_websocket(nullptr, local, *req)); //FIXME this cannot work as-is
                    }
                    throw std::exception(); //FIXME : proper failure
                });
            });
        });
    });
}*/

future<> httpd::websocket_input_stream::read_fragment() {
    auto parse_fragment = [this] {
        if (_buf.size() - _index > 2)
            _fragment = std::move(inbound_websocket_fragment(_buf, &_index));
    };

    _fragment.reset();
    if (!_buf || _index >= _buf.size())
        return _stream.read().then([this, parse_fragment](temporary_buffer<char> buf) {
            _buf = std::move(buf);
            _index = 0;
            parse_fragment();
        });
    parse_fragment();
    return make_ready_future();
}

future<httpd::websocket_message> httpd::websocket_input_stream::read() {
    _lastmassage.reset();
    return repeat([this] { // gather all fragments and concatenate full message
        return read_fragment().then([this] {
            if (!_fragment || _fragment._is_empty)
                return stop_iteration::yes;
            else if (_fragment.fin()) {
                if (!_lastmassage)
                    _lastmassage = std::move(websocket_message(std::move(_fragment)));
                else
                    _lastmassage.append(std::move(_fragment));
                return stop_iteration::yes;
            } else if (_fragment.opcode() == CONTINUATION)
                _lastmassage.append(std::move(_fragment));
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
future<> httpd::websocket_output_stream::write(httpd::websocket_message message) {
    message.done();
    return do_with(std::move(message), [this](httpd::websocket_message &frag) {
        temporary_buffer<char> head((char *) &frag._header, frag._header_size); //FIXME copy memory to avoid mixed writes
        return this->_stream.write(std::move(head)).then([this, &frag] {
            return do_for_each(frag._fragments, [this](temporary_buffer<char> &buff) {
                return this->_stream.write(std::move(buff));
            });
        }).then([this] {
            return this->_stream.flush();
        });
    }).handle_exception([this](std::exception_ptr e) {
        return _stream.close();
    });
}

future<> httpd::websocket_output_stream::write(websocket_opcode kind, temporary_buffer<char> buf) {
    return write(websocket_message(kind, std::move(buf)));
}