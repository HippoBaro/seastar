/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * Copyright 2017 Hippolyte Barraud
 */

#include "websocket.hh"
#include <cryptopp/sha.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/base64.h>

namespace seastar {
namespace httpd {
namespace websocket {
sstring encode_handshake_key(sstring nonce) {
    constexpr char uuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    constexpr size_t uuid_len = 36;

    CryptoPP::SHA hash;
    byte digest[CryptoPP::SHA::DIGESTSIZE];
    hash.Update((byte*) nonce.data(), nonce.size());
    hash.Update((byte*) uuid, uuid_len);
    hash.Final(digest);

    sstring base64;

    CryptoPP::Base64Encoder encoder;
    encoder.Put(digest, sizeof(digest));
    encoder.MessageEnd();
    CryptoPP::word64 size = encoder.MaxRetrievable();
    if (size) {
        base64.resize(size);
        encoder.Get((byte*) base64.data(), base64.size());
    }

    return base64.substr(0, base64.size() - 1); //fixme useless cpy
}

future<connected_socket>
connect(socket_address sa, socket_address local) {
    return engine().net().connect(sa, local).then([local](connected_socket fd) {
      seastar::input_stream<char> in = std::move(fd.input());
      seastar::output_stream<char> out = std::move(fd.output());
      return do_with(std::move(fd), std::move(in), std::move(out), [local](connected_socket& fd,
              auto& in,
              auto& out) {
        using random_bytes_engine = std::independent_bits_engine<
                std::default_random_engine, std::numeric_limits<unsigned char>::digits, unsigned char>;

        random_bytes_engine rbe;
        sstring key(16, '\0');
        std::generate(key.begin(), key.end(), std::ref(rbe));

        sstring nonce;
        CryptoPP::Base64Encoder encoder;
        encoder.Put((byte*) key.data(), key.size());
        encoder.MessageEnd();
        CryptoPP::word64 size = encoder.MaxRetrievable();
        if (size) {
            nonce.resize(size);
            encoder.Get((byte*) nonce.data(), nonce.size());
        } else {
            //fixme throw ?
        }
        nonce = nonce.substr(0, nonce.size() - 1);

        std::stringstream stream;
        //FIXME construct correct request header
        stream
                << "GET / HTTP/1.1\r\nHost: 127.0.0.1:10000\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                << "Sec-WebSocket-Key: " << nonce
                << "\r\nSec-WebSocket-Protocol: default\r\nSec-WebSocket-Version: 13\r\n\r\n";

        return out.write(stream.str()).then([local, nonce, &out, &in, &fd] {
          return out.flush();
        }).then([local, nonce, &in, &fd] {
          //FIXME extend http request parser to support header only payload (no HTTP verb)
          return in.read().then([local, nonce, &fd](temporary_buffer<char> response) {
            if (!response)
                throw std::exception(); //FIXME : proper failure
            if (std::experimental::string_view(response.begin(), response.size())
                    .find(encode_handshake_key(nonce)) != std::string::npos) {
                return std::move(fd);
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
}
}
}