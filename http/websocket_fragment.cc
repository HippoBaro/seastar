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
    _lenght = (uint64_t) (buf[*i] & 127);

    *i += sizeof(uint8_t);

    if (_lenght == 126 && raw.size() >= *i + sizeof(uint16_t)) {
        _lenght = net::ntoh(*reinterpret_cast<uint16_t*>(buf + *i));
        *i += sizeof(uint16_t);
    }
    else if (_lenght == 127 && raw.size() >= *i + sizeof(uint64_t)) {
        _lenght = net::ntoh(*reinterpret_cast<uint64_t*>(buf + *i));
        *i += sizeof(uint64_t);
    }

/*    if (_masked && raw.size() >= *i + length + sizeof(uint32_t)) { //message is masked
        uint64_t k = *i;
        *i += sizeof(uint32_t);
        message = std::move(raw.share(*i, length));
        un_mask(buf + *i, buf + *i, buf + k, length);
        *i += length;
    } else if (raw.size() >= *i + length) {
        message = std::move(raw.share(*i, length));
        *i += length;
    }*/
}

/*temporary_buffer<char> & httpd::websocket_message::concat() {
    if (!_concatenated.empty())
        return _concatenated;

    uint64_t length = 0;
    for (auto& n : _fragments)
        length += n.size();
    temporary_buffer<char> ret(length);
    length = 0;
    for (auto& n : _fragments) {
        std::memcpy(ret.share(length, n.size()).get_write(), n.get(), n.size());
        length += n.size();
    }
    concatenated = true;
    return (_concatenated = std::move(ret));
}*/


void httpd::un_mask(char *dst, const char *src, const char *mask, uint64_t length) {
    for (uint64_t j = 0; j < length; ++j)
        dst[j] = src[j] ^ mask[j % 4];
}
