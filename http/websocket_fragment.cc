//
// Created by hbarraud on 4/2/17.
//

#include "websocket_fragment.hh"

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

void httpd::inbound_websocket_fragment_base::parse(temporary_buffer<char> &raw, uint32_t *i) {
    auto buf = raw.get_write();

    //First header byte
    fin = (bool) (buf[*i] & 128);
    _rsv1 = (bool) (buf[*i] & 64);
    _opcode = static_cast<websocket_opcode>(buf[*i] & 15);

    *i += sizeof(uint8_t);

    //Second header byte
    _masked = (bool) (buf[*i] & 128);
    _length = (uint64_t) (buf[*i] & 127);

    *i += sizeof(uint8_t);

    if (_length == 126 && raw.size() >= *i + sizeof(uint16_t)) {
        _length = net::ntoh(*reinterpret_cast<uint16_t *>(buf + *i));
        *i += sizeof(uint16_t);
    } else if (_length == 127 && raw.size() >= *i + sizeof(uint64_t)) {
        _length = net::ntoh(*reinterpret_cast<uint64_t *>(buf + *i));
        *i += sizeof(uint64_t);
    }
}

void httpd::un_mask(char *dst, const char *src, const char *mask, uint64_t length) {
    uint32_t *dst_32 = (uint32_t *) dst;
    uint32_t *src_32 = (uint32_t *) src;
    uint32_t mask_32 = *reinterpret_cast<const uint32_t*>(mask);

    for (unsigned int k = 0; k < length >> 2; ++k) {
        dst_32[k] = src_32[k] ^ mask_32;
    }

    dst = dst + (length >> 2) * sizeof(uint32_t);
    src = src + (length >> 2) * sizeof(uint32_t);
    for (uint64_t j = 0; j < length % 4; ++j) {
        dst[j] = src[j] ^ mask[j % 4];
    }
}

// Extracted from https://www.cl.cam.ac.uk/~mgk25/ucs/utf8_check.c
// Licence : https://www.cl.cam.ac.uk/~mgk25/short-license.html
bool httpd::utf8_check(const unsigned char *s, size_t length)
{
    for (const unsigned char *e = s + length; s != e; ) {
        if (s + 4 <= e && ((*(uint32_t *) s) & 0x80808080) == 0) {
            s += 4;
        } else {
            while (!(*s & 0x80)) {
                if (++s == e) {
                    return true;
                }
            }
            if ((s[0] & 0x60) == 0x40) {
                if (s + 1 >= e || (s[1] & 0xc0) != 0x80 || (s[0] & 0xfe) == 0xc0) {
                    return false;
                }
                s += 2;
            } else if ((s[0] & 0xf0) == 0xe0) {
                if (s + 2 >= e || (s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80 ||
                    (s[0] == 0xe0 && (s[1] & 0xe0) == 0x80) || (s[0] == 0xed && (s[1] & 0xe0) == 0xa0)) {
                    return false;
                }
                s += 3;
            } else if ((s[0] & 0xf8) == 0xf0) {
                if (s + 3 >= e || (s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80 || (s[3] & 0xc0) != 0x80 ||
                    (s[0] == 0xf0 && (s[1] & 0xf0) == 0x80) || (s[0] == 0xf4 && s[1] > 0x8f) || s[0] > 0xf4) {
                    return false;
                }
                s += 4;
            } else {
                return false;
            }
        }
    }
    return true;
}

uint8_t httpd::websocket_message_base::write_payload_size() {
    assert(_header_size == 0 && "httpd::websocket_message_base::done() should be called exactly once");

    uint8_t advertised_size = 0;
    const auto header = opcode ^ 0x80; //FIXME Dynamically construct header
    _header[0] = header;

    size_t len = 0;
    for (auto &&item : fragments)
        len += item.size();

    if (fragments.size() < 125) { //Size fits 7bits
        advertised_size = (uint8_t) len;
        _header_size = 2;
    }
    else if (fragments.size() < std::numeric_limits<uint16_t>::max()) { //Size in extended to 16bits
        advertised_size = 126;
        auto s = net::hton(static_cast<uint16_t>(len));
        std::memmove(_header.data() + sizeof(uint16_t), &s, sizeof(uint16_t));
        _header_size = 4;
    }
    else { //Size extended to 64bits
        advertised_size = 127;
        auto l = net::hton(len);
        std::memmove(_header.data() + sizeof(uint16_t), &l, sizeof(uint64_t));
        _header_size = 10;
    }
    return advertised_size;
}
