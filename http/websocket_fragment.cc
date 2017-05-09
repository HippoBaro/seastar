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
httpd::inbound_websocket_fragment::inbound_websocket_fragment(temporary_buffer<char> &raw, uint32_t *i) :
        inbound_websocket_fragment() {
    auto buf = raw.get_write();

    //First header byte
    _fin = (bool) (buf[*i] & 128);
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

    if (_masked && raw.size() >= *i + _lenght + sizeof(uint32_t)) {
        //message is masked
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
    const auto header = opcode ^ 0x80; //FIXME Dynamically construct header

    assert(_header_size == 0 && "httpd::websocket_message::done() should be called exactly once");
    uint64_t len = 0;
    for (auto &fragment : _fragments)
        len += fragment.size();

    if (len < 125) { //Size fits 7bits
        _header[0] = header;
        _header[1] = net::hton(static_cast<uint8_t>(len));
        _header_size = 2;
    } //Size in extended to 16bits
    else if (len < std::numeric_limits<uint16_t>::max()) {
        _header[0] = header;
        _header[1] = 126;
        auto s = net::hton(static_cast<uint16_t>(len));
        std::memcpy(_header + sizeof(uint16_t), &s, sizeof(uint16_t));
        _header_size = 4;
    }
    else { //Size extended to 64bits
        _header[0] = header;
        _header[1] = 127;
        auto l = net::hton(len);
        std::memcpy(_header + sizeof(uint16_t), &l, sizeof(uint64_t));
        _header_size = 10;
    }
}

temporary_buffer<char> & httpd::websocket_message::concat() {
    if (!_concatenated.empty())
        return _concatenated;

    uint64_t length = 0;
    for (auto& n : _fragments)
        length += n.size();
    temporary_buffer<char> ret(length);
    length = 0;
    for (auto& n : _fragments) {
        std::memcpy(ret.share(length, n.size()).get_write(), n.get(), n.size());
        length = n.size();
    }
    return _concatenated = std::move(ret);
}


inline void httpd::inbound_websocket_fragment::unmask(char *dst, const char *src, const char *mask, uint64_t length) {
    for (uint64_t j = 0; j < length; ++j)
        dst[j] = src[j] ^ mask[j % 4];
}
