//
// Created by hippolyteb on 3/9/17.
//

#include "websocket.hh"
#include "http/request_parser.hh"
#include <cryptopp/sha.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/base64.h>

sstring
httpd::generate_websocket_key(sstring nonce) {
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
        encoder.Get((byte *) base64.data(), base64.size());
    }

    return base64.substr(0, base64.size() - 1); //fixme useless cpy
}

future<httpd::connected_websocket<httpd::websocket_type::CLIENT>>
httpd::connect_websocket(socket_address sa, socket_address local) {
    return engine().net().connect(sa, local).then([local] (connected_socket fd) {
        input_stream<char> in = std::move(fd.input());
        output_stream<char> out = std::move(fd.output());
        return do_with(std::move(fd), std::move(in), std::move(out), [local](connected_socket &fd,
                                                                             input_stream<char> &in,
                                                                             output_stream<char> &out) {
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
            nonce = nonce.substr(0, nonce.size() - 1);

            std::stringstream stream;
            //FIXME construct correct request header
            stream << "GET / HTTP/1.1\r\nHost: 127.0.0.1:10000\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                   << "Sec-WebSocket-Key: " << nonce << "\r\nSec-WebSocket-Protocol: default\r\nSec-WebSocket-Version: 13\r\n\r\n";

            return out.write(stream.str()).then([local, nonce, &out, &in, &fd] {
                return out.flush();
            }).then([local, nonce, &in, &fd] {
                //FIXME extend http request parser to support header only payload (no HTTP verb)
                return in.read().then([local, nonce, &fd] (temporary_buffer<char> response) {
                    if (!response)
                        throw std::exception(); //FIXME : proper failure
                    if (std::experimental::string_view(response.begin(), response.size())
                                .find(httpd::generate_websocket_key(nonce)) != std::string::npos) {
                        return httpd::connected_websocket<httpd::websocket_type::CLIENT>(std::move(fd), local);
                    } else {
                        fd.shutdown_input();
                        fd.shutdown_output();
                        throw std::exception(); //FIXME : proper failure
                    }
                });
            });
        });
    });
}

/*
 * When the write is called and (!_buf || _index >= _buf.size()) == false, it would make sense
 * to buff it and flush everything at once before the next read().
 */
future<> httpd::websocket_output_stream_base::write(httpd::websocket_message_base message) {
    return do_with(std::move(message), [this](httpd::websocket_message_base &frag) {
        temporary_buffer<char> head((char *) &frag._header, (size_t) frag._header_size); //FIXME copy memory to avoid mixed writes
        return _stream.write(std::move(head)).then([this, &frag] () -> future<> {
            //return do_for_each(frag.fragments, [this] (temporary_buffer<char> &partial) {
                return _stream.write(std::move(frag.payload));
            //});
        }).then([this] {
            return _stream.flush();
        });
    });
}
