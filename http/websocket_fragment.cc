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
httpd::inbound_websocket_fragment::inbound_websocket_fragment(temporary_buffer<char> raw) {
    uint64_t i = sizeof(uint16_t);

    for (std::size_t i = 0; i < raw.size(); ++i)
        std::cout << std::bitset<8>(raw.begin()[i]) << std::endl;

    std::bitset<8> header(raw.begin()[0]);
    std::cout << "header : " << header << std::endl;
    _fin = header.test(7);
    std::cout << "fin : " << _fin << std::endl;
    _rsv1 = header.test(6);
    std::cout << "rsv1 : " << rsv1() << std::endl;
    _rsv2 = header.test(5);
    std::cout << "rsv2 : " << rsv2() << std::endl;
    _rsv3 = header.test(4);
    std::cout << "rsv3 : " << rsv3() << std::endl;
    _opcode = static_cast<websocket_opcode>(header.reset(7).reset(6).reset(5).reset(4).to_ulong());
    std::cout << "opcode : " << _opcode << std::endl;

    header = std::bitset<8>(raw.begin()[1]);
    std::cout << "header : " << header << std::endl;
    _masked = header.test(7);
    std::cout << "masked : " << _masked << std::endl;
    header = header.reset(7);
    std::cout << "header : " << header << std::endl;

    if (header.to_ulong() < 126) {
        _lenght = header.to_ulong();
        std::cout << "lenght : " << _lenght << std::endl;
    }
    else if (header.to_ulong() == 126) {
        _lenght = *((uint16_t *) raw.share(i, sizeof(uint16_t)).get());
        std::cout << "lenght : " << _lenght << std::endl;
        i += sizeof(uint16_t);
    }
    else if (header.to_ulong() == 127) {
        _lenght = *((uint64_t *) raw.share(i, sizeof(uint64_t)).get());
        std::cout << "lenght : " << _lenght << std::endl;
        i += sizeof(uint64_t);
    }

    if (_masked) {
        _maskkey = temporary_buffer<char>(std::move(raw.share(i, sizeof(uint32_t))));
        i += sizeof(uint32_t);

        message = temporary_buffer<char>(std::move(raw.share(i, _lenght)));
        for (uint64_t j = 0; j < _lenght; ++j)
            message.get_write()[j] = message[j] ^ _maskkey[j%4];
        std::cout << std::endl;
    } else
        message = temporary_buffer<char>(std::move(raw.share(i, _lenght)));
    std::cout << "END PARSING OF FRAGMENT" << std::endl;
}

temporary_buffer<char> httpd::outbound_websocket_fragment::get_header() {
    auto header = get_header_internal();
    std::cout << "payload lenght : " << _lenght << std::endl;
    if (_lenght < 125) { //Size fits 7bits
        temporary_buffer<char> buff(2);
        buff.get_write()[0] = header;
        std::bitset<8> byte2(static_cast<unsigned char>(_lenght));
        std::cout << "byte2 : " << byte2 << std::endl;
        byte2[7] = _masked;
        buff.get_write()[1] = static_cast<unsigned char>(byte2.to_ulong());

        for(auto c : buff){
            std::bitset<8> bitset(c);
            std::cout << bitset << std::endl;
        }

        for(auto c : message){
            std::bitset<8> bitset(c);
            std::cout << bitset << std::endl;
        }

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
