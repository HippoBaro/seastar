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

#include "websocket_message.hh"

namespace seastar {
namespace httpd {
namespace websocket {

uint8_t message_base::write_header(char* header) {
    uint8_t advertised_size = 0;
    header[0] = opcode ^ (fin ? 0x80 : 0x0);

    if (payload.size() < 126) { //Size fits 7bits
        advertised_size = (uint8_t)payload.size();
        _header_size = 2;
    } else if (payload.size() <= std::numeric_limits<uint16_t>::max()) { //Size is extended to 16bits
        advertised_size = 126;
        auto s = net::hton(static_cast<uint16_t>(payload.size()));
        std::memmove(header + sizeof(uint16_t), &s, sizeof(uint16_t));
        _header_size = 4;
    } else { //Size extended to 64bits
        advertised_size = 127;
        auto l = net::hton(payload.size());
        std::memmove(header + sizeof(uint16_t), &l, sizeof(uint64_t));
        _header_size = 10;
    }
    return advertised_size;
}

// Extracted from https://www.cl.cam.ac.uk/~mgk25/ucs/utf8_check.c
// Licence : https://www.cl.cam.ac.uk/~mgk25/short-license.html
bool utf8_check(const unsigned char* s, size_t length) {
    for (const unsigned char* e = s + length; s != e;) {
        if (s + 4 <= e && ((*(uint32_t*) s) & 0x80808080) == 0) {
            s += 4;
        } else {
            while (!(*s & 0x80)) { if (++s == e) { return true; } }
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
            } else { return false; }
        }
    }
    return true;
}

}
}
}