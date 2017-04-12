//
// Created by hbarraud on 4/2/17.
//

#include "websocket_fragment.hh"

/*    0                   1                   2                   3
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
httpd::inbound_websocket_fragment::inbound_websocket_fragment(temporary_buffer<char> &raw, uint32_t *i) : websocket_fragment_base() {
    auto buf = raw.get_write();

    //First header byte
    _fin = buf[*i] & 128;
    _rsv1 = buf[*i] & 64;
    _opcode = static_cast<websocket_opcode>(buf[*i] & 15);
    *i += sizeof(uint8_t);

    //Second header byte
    _masked = buf[*i] & 128;
    _lenght = buf[*i] & 127;
    *i += sizeof(uint8_t);

    if (_lenght == 126 && raw.size() >= *i + sizeof(uint16_t)) {
        _lenght = ntohs(*reinterpret_cast<uint16_t*>(buf + *i));
        *i += sizeof(uint16_t);
    }
    else if (_lenght == 127 && raw.size() >= *i + sizeof(uint64_t)) {
        _lenght = ntohl(*reinterpret_cast<uint64_t*>(buf + *i));
        *i += sizeof(uint64_t);
    }

    if (_masked && raw.size() >= *i + _lenght + sizeof(uint32_t)) {
        uint64_t k = *i;
        *i += sizeof(uint32_t);

        message = std::move(raw.share(*i, _lenght));

        unmask(buf + *i, buf + *i, buf + k, _lenght);
        _is_empty = false;
        *i += _lenght;
    } else if (raw.size() >= *i + _lenght) {
        message = std::move(raw.share(*i, _lenght));
        _is_empty = false;
        *i += _lenght;
    }
}

void httpd::websocket_message::done() {
    auto header = 0x81;

    uint64_t len = 0;
    for (auto &fragment : _fragments)
        len += fragment.size();

    if (len < 125) { //Size fits 7bits
        _header[0] = header;
        _header[1] = static_cast<unsigned char>(len);
        _header_size = 2;
    } //Size in extended to 16bits
    else if (len < std::numeric_limits<uint16_t>::max()) {
        _header[0] = header;
        _header[1] = static_cast<unsigned char>(126);
        auto s = htons(len);
        std::memcpy(_header + 2, &s, 2);
        _header_size = 4;
    }
    else { //Size extended to 64bits
        _header[0] = header;
        _header[1] = static_cast<unsigned char>(127);
        auto l = htonl(len);
        std::memcpy(_header + 2, &l, 8);
        _header_size = 10;
    }
}

temporary_buffer<char> httpd::websocket_message::concat() {
    std::cout << "copying" << std::endl;
    uint64_t length = 0;
    for (auto& n : _fragments)
        length += n.size();
    temporary_buffer<char> ret(length);
    length = 0;
    for (auto& n : _fragments) {
        std::memcpy(ret.share(length, n.size()).get_write(), n.get(), n.size());
        length = n.size();
    }
    return std::move(ret);
}


inline void httpd::inbound_websocket_fragment::unmask(char *dst, const char *src, const char *mask, uint64_t length) {
    for (uint64_t j = 0; j < _lenght; ++j) {
        dst[j] = src[j] ^ mask[j % 4];
    }
/*    for (unsigned int n = (length >> 2) + 1; n; n--) {
        *(dst++) = *(src++) ^ mask[0];
        *(dst++) = *(src++) ^ mask[1];
        *(dst++) = *(src++) ^ mask[2];
        *(dst++) = *(src++) ^ mask[3];
    }*/
}
