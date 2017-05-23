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

namespace seastar {
namespace httpd {
namespace websocket {

inbound_fragment_base::inbound_fragment_base(fragment_header const& header, temporary_buffer<char>& payload) noexcept:
        header(header), message(std::move(payload)) { }

void un_mask(char* dst, const char* src, const char* mask, uint64_t length) {
    uint32_t* dst_32 = (uint32_t*) dst;
    uint32_t* src_32 = (uint32_t*) src;
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
bool utf8_check(const unsigned char* s, size_t length) {
    for (const unsigned char* e = s + length; s != e;) {
        if (s + 4 <= e && ((*(uint32_t*) s) & 0x80808080) == 0) {
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


}
}
}
