#include "websocket_message.hh"

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