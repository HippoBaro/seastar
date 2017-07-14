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

#include "websocket_fragment.hh"

#pragma once

namespace seastar {

namespace httpd {

namespace websocket {

/**
 * Mask or unmask a buffer with the provided masking key.
 * Also, it is used as a concatenation utility, effectively masking/unmasking _and_ copying buffers to concatenate
 * fragmented messages.
 * @param dst pointer to the destination buffer where data are to be copied
 * @param src pointer to the currently masked data buffer.
 * @param mask pointer to the 4-bytes masking key.
 * @param length how many bytes to mask/unmask.
 */
inline void un_mask(char* dst, const char* src, const char* mask, uint64_t length) {
    //TODO good candidate for SIMD
    // Did try to process 32 bits at a time but got aliasing errors.
    for (uint64_t j = 0; j < length; ++j) {
        dst[j] = src[j] ^ mask[j % 4];
    }
}

/**
 * Utility function. Check for UTF-8 encoding
 * Extracted from https://www.cl.cam.ac.uk/~mgk25/ucs/utf8_check.c
 * Licence : https://www.cl.cam.ac.uk/~mgk25/short-license.html
 * @param s pointer to the buffer that is to be checked against UTF-8 encoding
 * @param length how many bytes to check.
 * @return returns true if the data is a valid UTF-8 encoded string, false otherwise.
 */
bool utf8_check(const unsigned char* s, size_t length);

class message_base {
public:
    websocket::opcode opcode;
    uint8_t header_size = 0;
    temporary_buffer<char> payload;
    ///Only use when sending message, always true when receiving.
    bool fin = true;

    message_base(websocket::opcode opcode, temporary_buffer<char> payload, bool fin = true) noexcept :
            opcode(opcode), payload(std::move(payload)), fin(fin) {
    };

    message_base(websocket::opcode opcode, sstring message = "", bool fin = true) noexcept :
            opcode(opcode), payload(std::move(std::move(message).release())), fin(fin) {
    };

    message_base() = default;

    message_base(const message_base&) = delete;

    message_base(message_base&& other) noexcept :
            opcode(other.opcode), header_size(other.header_size), payload(std::move(other.payload)), fin(other.fin) {
    }

    void operator=(const message_base&) = delete;

    message_base& operator=(message_base&& other) noexcept {
        if (this != &other) {
            opcode = other.opcode;
            header_size = other.header_size;
            payload = std::move(other.payload);
            fin = other.fin;
        }
        return *this;
    }

    explicit operator bool() const { return !payload.empty(); }

    uint8_t write_header(char* header);
};

template<websocket::endpoint_type type>
class message final : public message_base {
};

template<>
class message<endpoint_type::CLIENT> final : public message_base {
    using message_base::message_base;
public:
    message() = default;

    message(std::vector<websocket::inbound_fragment>& fragments) :
            message_base(fragments.front().header.opcode, temporary_buffer<char>(
                    std::accumulate(fragments.begin(), fragments.end(), 0,
                            [](size_t x, inbound_fragment& y) {
                              return x + y.message.size();
                            }))) {
        uint64_t k = 0;
        char* buf = payload.get_write();
        for (unsigned int j = 0; j < fragments.size(); ++j) {
            //SERVER never mask data, so we concatenate all fragments to assemble the final message
            std::memcpy(buf + k, fragments[j].message.get(), fragments[j].message.size());
            k += fragments[j].message.size();
        }
        if (opcode == websocket::opcode::TEXT && !utf8_check((const unsigned char*)buf, payload.size())) {
            throw websocket_exception(close_status_code::INCONSISTENT_DATA);
        }
    }

    message(inbound_fragment& fragment) : message_base(fragment.header.opcode, std::move(fragment.message)) {
        if (opcode == websocket::opcode::TEXT && !utf8_check((const unsigned char*)payload.get(), payload.size())) {
            throw websocket_exception(close_status_code::INCONSISTENT_DATA);
        }
    }

    temporary_buffer<char> get_header() {
        temporary_buffer<char> header(14);
        auto wr = header.get_write();

        wr[1] = (char)(128 | write_header(wr));
        //FIXME Constructing an independent_bits_engine is expensive. static thread_local ?
        static thread_local std::independent_bits_engine<std::default_random_engine, std::numeric_limits<uint32_t>::digits, uint32_t> rbe;
        uint32_t mask = rbe();
        std::memcpy(wr + header_size, &mask, sizeof(uint32_t));
        header_size += sizeof(uint32_t);
        un_mask(payload.get_write(), payload.get(), (char*)(&mask), payload.size());
        header.trim(header_size);
        return std::move(header);
    }
};

template<>
class message<endpoint_type::SERVER> final : public message_base {
    using message_base::message_base;
public:

    message() = default;

    message(std::vector<websocket::inbound_fragment>& fragments) :
            message_base(fragments.front().header.opcode, temporary_buffer<char>(
                    std::accumulate(fragments.begin(), fragments.end(), 0,
                            [](size_t x, inbound_fragment& y) {
                              return x + y.message.size();
                            }))) {
        uint64_t k = 0;
        char* buf = payload.get_write();
        for (unsigned int j = 0; j < fragments.size(); ++j) {
            un_mask(buf + k, fragments[j].message.get(), (char*)(&fragments[j].header.mask_key),
                    fragments[j].message.size());
            k += fragments[j].message.size();
        }
        if (opcode == websocket::opcode::TEXT && !utf8_check((const unsigned char*)buf, payload.size())) {
            throw websocket_exception(close_status_code::INCONSISTENT_DATA);
        }
    }

    message(inbound_fragment& fragment) :
            message_base(fragment.header.opcode, temporary_buffer<char>(fragment.message.size())) {
        un_mask(payload.get_write(), fragment.message.get(), (char*)(&fragment.header.mask_key), payload.size());
        if (opcode == websocket::opcode::TEXT && !utf8_check((const unsigned char*)payload.get(), payload.size())) {
            throw websocket_exception(close_status_code::INCONSISTENT_DATA);
        }
    }

    temporary_buffer<char> get_header() {
        temporary_buffer<char> header(14);
        auto wr = header.get_write();

        wr[1] = write_header(wr);
        header.trim(header_size);

        return std::move(header);
    }
};

/**
 * Utility function that construct a CLOSE message.
 * @tparam type whether the message to create is from a SERVER or CLIENT websocket.
 * @param code The close reason
 * @return A new CLOSE message that has the 16-bits close reason encoded inside it's payload.
 */
template<websocket::endpoint_type type>
static message<type> make_close_message(close_status_code code = close_status_code::NORMAL_CLOSURE) {
    if (code == close_status_code::NONE)
        return message<type>(opcode::CLOSE);
    sstring payload(sizeof(uint16_t), '\0');
    *(reinterpret_cast<uint16_t*>(payload.begin())) = net::hton((uint16_t)code);
    return message<type>(opcode::CLOSE, std::move(payload));
}

}
}
}