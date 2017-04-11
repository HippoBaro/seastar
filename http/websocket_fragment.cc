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
    //_rsv2 = header.test(5);
    //_rsv3 = header.test(4);
    _opcode = static_cast<websocket_opcode>(buf[*i] & 15);
    *i += sizeof(uint8_t);

    //Second header byte
    _masked = buf[*i] & 128;
    _lenght = buf[*i] & 127;
    *i += sizeof(uint8_t);

    if (_lenght == 126 && raw.size() >= *i + sizeof(uint16_t)) {
        _lenght = *((uint16_t *) raw.share(*i, sizeof(uint16_t)).get());
        *i += sizeof(uint16_t);
    }
    else if (_lenght == 127 && raw.size() >= *i + sizeof(uint64_t)) {
        _lenght = *((uint64_t *) raw.share(*i, sizeof(uint64_t)).get());
        *i += sizeof(uint64_t);
    }

    if (_masked && raw.size() >= *i + _lenght + sizeof(uint32_t)) {
        //_maskkey = std::move(raw.share(*i, sizeof(uint32_t)));
        uint64_t k = *i;
        *i += sizeof(uint32_t);

        //std::cout << "index is " << *i << std::endl;
        message = std::move(raw.share(*i, _lenght));

        for (uint64_t j = 0; j < _lenght; ++j)
            buf[j] = buf[j] ^ buf[(k - j) % 4];
        _is_empty = false;
        *i += _lenght;
    } else if (raw.size() >= *i + _lenght) {
        message = std::move(raw.share(*i, _lenght));
        _is_empty = false;
        *i += _lenght;
    }
}
