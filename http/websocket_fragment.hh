//
// Created by hbarraud on 4/2/17.
//

#pragma once

#include <core/reactor.hh>
#include <random>

namespace seastar {
namespace httpd {
namespace websocket {

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
            status_code(status_code) { }
};

class fragment_header {
public:
    bool fin;
    bool rsv1;
    bool rsv23;
    websocket::opcode opcode;
    bool masked;
    uint64_t length;
    uint32_t mask_key = 0;

    fragment_header() = default;

    fragment_header(temporary_buffer<char>& header) :
            fin(header[0] & 128),
            rsv1(header[0] & 64),
            rsv23(header[0] & 48),
            opcode(static_cast<websocket::opcode>(header[0] & 15)),
            masked(header[1] & 128),
            length(header[1] & 127) { }

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
            length = net::ntoh(*reinterpret_cast<const uint16_t*>(extended_header.get()));
        } else if (length == 127 && extended_header.size() >= sizeof(uint64_t)) {
            length = net::ntoh(*reinterpret_cast<const uint64_t*>(extended_header.get()));
        }
        if (masked) {
            mask_key = *reinterpret_cast<const uint32_t*>(extended_header.end() - sizeof(uint32_t));
        }
    }
};

void un_mask(char* dst, const char* src, const char* mask, uint64_t length);

bool utf8_check(const unsigned char* s, size_t length);

class inbound_fragment_base {
    friend class input_stream_base;

public:
    fragment_header header;
    temporary_buffer<char> message;

    inbound_fragment_base(fragment_header const& header, temporary_buffer<char>& payload) noexcept;

    inbound_fragment_base(const inbound_fragment_base&) = delete;

    inbound_fragment_base(inbound_fragment_base&& fragment) noexcept :
            header(fragment.header), message(std::move(fragment.message)) { }

    inbound_fragment_base() = default;

    inbound_fragment_base& operator=(const inbound_fragment_base&) = delete;

    inbound_fragment_base& operator=(inbound_fragment_base&& fragment) noexcept {
        if (*this != fragment) {
            header = fragment.header;
            message = std::move(fragment.message);
        }
        return *this;
    }

    operator bool() {
        return !((header.rsv1 || header.rsv23 || (header.opcode > 2 && header.opcode < 8)
                || header.opcode > 10 || (header.opcode > 2 && (!header.fin || message.size() > 125))));
    }
};

template<websocket::endpoint_type type>
class inbound_fragment final : public inbound_fragment_base {
};

template<>
class inbound_fragment<CLIENT> final : public inbound_fragment_base {
    using inbound_fragment_base::inbound_fragment_base;
public:
    inbound_fragment() noexcept { }

    inbound_fragment(fragment_header const& header, temporary_buffer<char>& payload) noexcept:
            inbound_fragment_base(header, payload) { }
};

template<>
class inbound_fragment<SERVER> final : public inbound_fragment_base {
    using inbound_fragment_base::inbound_fragment_base;
public:
    inbound_fragment() { }

    inbound_fragment(fragment_header const& header, temporary_buffer<char>& payload) noexcept:
            inbound_fragment_base(header, payload) { }
};

}
}
}

