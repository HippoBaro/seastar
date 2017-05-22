#include "websocket_message.hh"

uint8_t httpd::websocket_message_base::write_payload_size(char* header) {
    uint8_t advertised_size = 0;
    header[0] = opcode ^ (fin ? 0x80 : 0x0);

    if (payload.size() < 126) { //Size fits 7bits
        advertised_size = (uint8_t) payload.size();
        _header_size = 2;
    } else if (payload.size() <= std::numeric_limits<uint16_t>::max()) { //Size in extended to 16bits
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