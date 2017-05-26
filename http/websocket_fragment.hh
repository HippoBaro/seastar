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
 * Copyright 2015 Cloudius Systems
 */

#pragma once

#include <core/reactor.hh>
#include <random>

namespace seastar {
namespace httpd {
namespace websocket {

/*
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-------+-+-------------+-------------------------------+
|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
|I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
|N|V|V|V|       |S|             |   (if payload len==126/127)   |
| |1|2|3|       |K|             |                               |
+-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
|     Extended payload length continued, if payload len == 127  |
+ - - - - - - - - - - - - - - - +-------------------------------+
|                               |Masking-key, if MASK set to 1  |
+-------------------------------+-------------------------------+
| Masking-key (continued)       |          Payload Data         |
+-------------------------------- - - - - - - - - - - - - - - - +
:                     Payload Data continued ...                :
+ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
|                     Payload Data continued ...                |
+---------------------------------------------------------------+
*/

enum close_status_code : uint16_t {
    NORMAL_CLOSURE = 1000,
    GOING_AWAY = 1001,
    PROTOCOL_ERROR = 1002,
    CANNOT_ACCEPT = 1003,
    INCONSISTENT_DATA = 1007,
    POLICY_VIOLATION = 1008,
    MESSAGE_TOO_BIG = 1009,
    EXPECTED_EXTENSION = 1010,
    UNEXPECTED_CONDITION = 1011,
    NONE
};

enum endpoint_type {
    SERVER,
    CLIENT
};

enum opcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA,
    RESERVED = 0xB
};

class websocket_exception final : public std::exception {
public:
    close_status_code status_code;

    websocket_exception(close_status_code status_code = NORMAL_CLOSURE) noexcept :
            status_code(status_code) {}
};

/**
 * Represent a full fragment header.
 * It's initialized with a 2 bytes buffer. If necessary, it can be extended to support longer fragment headers.
 */
class fragment_header {
public:
    /// Is this the last fragment
    bool fin;
    /// Reserved bit 1 (used for extensions)
    bool rsv1;
    /// Reserved bits 2 and 3 (reserved, never set)
    bool rsv23;
    /// Opcode of the fragment
    websocket::opcode opcode;
    /// Is the fragment payload masked
    bool masked;
    /// The total length of the fragment's payload. Can change when feeding an extended header
    uint64_t length;
    /// The masking key, if payload is masked
    uint32_t mask_key = 0;

    fragment_header() = default;

    fragment_header(temporary_buffer<char>& header) noexcept :
            fin(static_cast<bool>(header[0] & 128)),
            rsv1(static_cast<bool>(header[0] & 64)),
            rsv23(static_cast<bool>(header[0] & 48)),
            opcode(static_cast<websocket::opcode>(header[0] & 15)),
            masked(static_cast<bool>(header[1] & 128)),
            length(static_cast<uint8_t>(header[1] & 127)) {}

    uint8_t extended_header_length_size() {
        uint8_t ret = 0;
        if (length == 126) ret += sizeof(uint16_t); // Extended length is 16 bits.
        else if (length == 127) ret += sizeof(uint64_t); // Extended length is 64 bits.
        return ret;
    }

    uint8_t extended_header_size() {
        uint8_t ret = extended_header_length_size();
        if (masked) ret += sizeof(uint32_t); // Mask key is 32bits.
        return ret;
    }

    void feed_extended_header(temporary_buffer<char>& extended_header) {
        if (length == 126 && extended_header.size() >= sizeof(uint16_t)) {
            uint16_t len;
            std::memcpy(&len, extended_header.get(), sizeof(uint16_t));
            length = net::ntoh(len);
        } else if (length == 127 && extended_header.size() >= sizeof(uint64_t)) {
            std::memcpy(&length, extended_header.get(), sizeof(uint64_t));
            length = net::ntoh(length);
        }
        if (masked) {
            std::memcpy(&mask_key, extended_header.end() - sizeof(uint32_t), sizeof(uint32_t));
        }
    }
};

class inbound_fragment_base {
    friend class input_stream_base;

public:
    fragment_header header;
    temporary_buffer<char> message;

    inbound_fragment_base(fragment_header const& header, temporary_buffer<char>& payload) noexcept :
            header(header), message(std::move(payload)) { }

    inbound_fragment_base(const inbound_fragment_base&) = delete;

    inbound_fragment_base(inbound_fragment_base&& fragment) noexcept :
            header(fragment.header), message(std::move(fragment.message)) {}

    inbound_fragment_base() = default;

    inbound_fragment_base& operator=(const inbound_fragment_base&) = delete;

    inbound_fragment_base& operator=(inbound_fragment_base&& fragment) noexcept {
        if (*this != fragment) {
            header = fragment.header;
            message = std::move(fragment.message);
        }
        return *this;
    }

    /**
     * Basic fragment protocol check. Does NOT means that the fragment payload is empty nor advertises EOF.
     * @return true if fragment is valid, false otherwise
     */
    operator bool() {
        return !((header.rsv1 || header.rsv23 || (header.opcode > 2 && header.opcode < 8)
                || header.opcode > 10 || (header.opcode > 2 && (!header.fin || message.size() > 125))));
    }
};

/**
 * This specialization is unused right now but could prove useful when extension comes into play.
 */
template<websocket::endpoint_type type>
class inbound_fragment final : public inbound_fragment_base {
    using inbound_fragment_base::inbound_fragment_base;

};

}
}
}

