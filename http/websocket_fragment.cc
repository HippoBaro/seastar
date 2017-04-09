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
    //std::cout << "index is " << *i << std::endl;

    std::bitset<8> header(raw.begin()[*i]);
    _fin = header.test(7);
    _rsv1 = header.test(6);
    _rsv2 = header.test(5);
    _rsv3 = header.test(4);
    _opcode = static_cast<websocket_opcode>(header.reset(7).reset(6).reset(5).reset(4).to_ulong());
    *i += sizeof(uint8_t);

    header = std::bitset<8>(raw.begin()[*i]);
    _masked = header.test(7);
    header = header.reset(7);
    *i += sizeof(uint8_t);

    if (header.to_ulong() < 126) {
        _lenght = header.to_ulong();
    }
    else if (header.to_ulong() == 126 && raw.size() >= *i + sizeof(uint16_t)) {
        _lenght = *((uint16_t *) raw.share(*i, sizeof(uint16_t)).get());
        *i += sizeof(uint16_t);
    }
    else if (header.to_ulong() == 127 && raw.size() >= *i + sizeof(uint64_t)) {
        _lenght = *((uint64_t *) raw.share(*i, sizeof(uint64_t)).get());
        *i += sizeof(uint64_t);
    }

    //std::cout << "payload length is " << raw.size() << " and fragment announced length is " << _lenght << ". FIN is " << _fin << std::endl;

    if (_masked && raw.size() >= *i + _lenght + sizeof(uint32_t)) {
        _maskkey = std::move(raw.share(*i, sizeof(uint32_t)));
        *i += sizeof(uint32_t);

        //std::cout << "index is " << *i << std::endl;
        message = std::move(raw.share(*i, _lenght));
        auto buf = message.get_write();
        for (uint64_t j = 0; j < _lenght; ++j)
            buf[j] = buf[j] ^ _maskkey[j%4];
        _is_empty = false;
        *i += _lenght;
    } else if (raw.size() >= *i + _lenght) {
        message = std::move(raw.share(*i, _lenght));
        _is_empty = false;
        *i += _lenght;
    }
}

temporary_buffer<char> httpd::outbound_websocket_fragment::get_header() {
    auto header = get_header_internal();
    if (_lenght < 125) { //Size fits 7bits
        temporary_buffer<char> buff(2);
        buff.get_write()[0] = header;
/*        std::bitset<8> byte2(static_cast<unsigned char>(_lenght));
        byte2[7] = _masked;*/
        buff.get_write()[1] = static_cast<unsigned char>(_lenght);

        return std::move(buff);
    } //Size in extended to 16bits
    else if (_lenght < std::numeric_limits<uint16_t>::max()) {
        temporary_buffer<char> buff(4);
        buff.get_write()[0] = header;
        std::bitset<8> byte2(126);
        byte2[7] = _masked;
        buff.get_write()[1] = static_cast<unsigned char>(byte2.to_ulong());
        std::memcpy(buff.share(2, 2).get_write(), &_lenght, 2);

        return std::move(buff);
    }
    else { //Size extended to 64bits
        temporary_buffer<char> buff(10);
        buff.get_write()[0] = header;
        std::bitset<8> byte2(126);
        byte2[7] = _masked;
        buff.get_write()[1] = static_cast<unsigned char>(byte2.to_ulong());
        std::memcpy(buff.share(2, 8).get_write(), &_lenght, 8);

        return std::move(buff);
    }
}