#include "websocket_fragment.hh"

#ifndef SEASTAR_WEBSOCKET_MESSAGE_HH
#define SEASTAR_WEBSOCKET_MESSAGE_HH

namespace httpd {
class websocket_message_base {
public:
  websocket_opcode opcode = RESERVED;
  int _header_size = 0;
  std::array<char, 14> _header;
  std::vector<temporary_buffer<char>> fragments;

  websocket_message_base() = default;
  websocket_message_base(const websocket_message_base &) = delete;
  websocket_message_base(websocket_message_base &&other) noexcept : opcode(other.opcode),
    _header_size(other._header_size),
    _header(other._header),
    fragments(std::move(other.fragments)) {
  }

  void operator=(const websocket_message_base&) = delete;
  websocket_message_base & operator= (websocket_message_base &&other) {
    if (this != &other) {
      opcode = other.opcode;
      _header_size = other._header_size;
      _header = other._header;
      fragments = std::move(other.fragments);
    }
    return *this;
  }

  explicit operator bool() const { return !fragments.empty(); }

  websocket_message_base(inbound_websocket_fragment_base fragment) noexcept :
    websocket_message_base(fragment.opcode(), std::move(fragment.message)) {
  }

  websocket_message_base(websocket_opcode kind, sstring message) noexcept :
    websocket_message_base(kind, std::move(message).release()) {
  }

  websocket_message_base(websocket_opcode kind, temporary_buffer<char> message) noexcept : opcode(kind) {
    fragments.push_back(std::move(message));
  }

  void append(inbound_websocket_fragment_base fragment) {
    fragments.emplace_back(fragment.message.begin(), fragment.message.size());
  }

  void reset() {
    opcode = RESERVED;
    _header_size = 0;
    fragments.clear();
  }

  uint8_t write_payload_size();
};

template<websocket_type type>
class websocket_message : public websocket_message_base {};

template<>
class websocket_message<CLIENT> final : public websocket_message_base {
  using websocket_message_base::websocket_message_base;
public:
  void done() {
    _header[1] = (char) (128 | write_payload_size());
    //FIXME Constructing an independent_bits_engine is expensive. static thread_local ?
    std::independent_bits_engine<std::default_random_engine, std::numeric_limits<uint32_t>::digits, uint32_t> rbe;
    uint32_t mask = rbe();
    std::memcpy(_header.data() + _header_size, &mask, sizeof(uint32_t));
    _header_size += sizeof(uint32_t);
    //fixme mask

    if (fragments.empty())
      return;
    un_mask(fragments.front().get_write(), fragments.front().get(), (char *) (&mask), fragments.front().size());
  }
};

template<>
class websocket_message<SERVER> final : public websocket_message_base {
private:
  temporary_buffer<char> _payload;
public:
  using websocket_message_base::websocket_message_base;
  void done() {
    _header[1] = write_payload_size();
  };

  temporary_buffer<char>& concat() {
    if (fragments.size() == 1) {
      return fragments.front();
    }

    uint64_t lenght = 0;
    for (auto&& fragment : fragments) {
      lenght += fragment.size();
    }

    _payload = temporary_buffer<char>(lenght);

    uint64_t k = 0;
    char* buf = _payload.get_write();
    for (unsigned int j = 0; j < fragments.size(); ++j) {
      std::memmove(buf + k, fragments[j].get(), fragments.size());
      k += fragments.size();
    }
    return _payload;
  }
};
}

#endif
