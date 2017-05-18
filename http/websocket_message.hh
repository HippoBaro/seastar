#include "websocket_fragment.hh"

#ifndef SEASTAR_WEBSOCKET_MESSAGE_HH
#define SEASTAR_WEBSOCKET_MESSAGE_HH

namespace httpd {
    class websocket_message_base {
    public:
        websocket_opcode opcode = RESERVED;
        int _header_size = 0;
        std::array<char, 14> _header;
        temporary_buffer<char> payload;

        websocket_message_base(websocket_opcode opcode) noexcept : opcode(opcode) {};

        websocket_message_base(websocket_opcode opcode, sstring message) noexcept :
                opcode(opcode), payload(std::move(std::move(message).release())) {};

        websocket_message_base() noexcept {};

        websocket_message_base(const websocket_message_base &) = delete;

        websocket_message_base(websocket_message_base &&other) noexcept : opcode(other.opcode),
                                                                          _header_size(other._header_size),
                                                                          _header(other._header),
                                                                          payload(std::move(other.payload)) {
        }

        void operator=(const websocket_message_base &) = delete;

        websocket_message_base &operator=(websocket_message_base &&other) noexcept {
            if (this != &other) {
                opcode = other.opcode;
                _header_size = other._header_size;
                _header = other._header;
                payload = std::move(other.payload);
            }
            return *this;
        }

        explicit operator bool() const { return !payload.empty(); }

        void reset() {
            opcode = RESERVED;
            _header_size = 0;
            payload = temporary_buffer<char>();
        }

        uint8_t write_payload_size();
    };

    template<websocket_type type>
    class websocket_message final : public websocket_message_base {
    };

    template<>
    class websocket_message<CLIENT> final : public websocket_message_base {
    public:
        using websocket_message_base::websocket_message_base;

        websocket_message() noexcept {}

        websocket_message(std::vector<inbound_websocket_fragment<CLIENT>> & fragments):
                websocket_message_base(fragments.back().opcode())
        {
            if (fragments.size() == 1) {
                payload = std::move(fragments.back().message);
            }
            else {
                uint64_t lenght = 0;
                for (auto &&fragment : fragments) {
                    lenght += fragment.message.size();
                }

                payload = temporary_buffer<char>(lenght);

                uint64_t k = 0;
                char *buf = payload.get_write();
                for (unsigned int j = 0; j < fragments.size(); ++j) {
                    std::memmove(buf + k, fragments[j].message.get(), fragments.size());
                    k += fragments.size();
                }
            }
            if (opcode == websocket_opcode::TEXT && !utf8_check((const unsigned char *) payload.get(), payload.size())) {
                throw std::exception();
            }
        }

        void done() {
            _header[1] = (char) (128 | write_payload_size());
            //FIXME Constructing an independent_bits_engine is expensive. static thread_local ?
            std::independent_bits_engine<std::default_random_engine, std::numeric_limits<uint32_t>::digits, uint32_t> rbe;
            uint32_t mask = rbe();
            std::memcpy(_header.data() + _header_size, &mask, sizeof(uint32_t));
            _header_size += sizeof(uint32_t);
            un_mask(payload.get_write(), payload.get(), (char *) (&mask), payload.size());
        }
    };

    template<>
    class websocket_message<SERVER> final : public websocket_message_base {
    public:
        using websocket_message_base::websocket_message_base;

        websocket_message() noexcept {}

        websocket_message(std::vector<inbound_websocket_fragment<SERVER>>& fragments):
                websocket_message_base(fragments.back().opcode())
        {
            if (fragments.size() == 1) {
                payload = temporary_buffer<char>(fragments.back().message.size());
                un_mask(payload.get_write(), fragments.back().message.get(), (char *) (&fragments.back().mask_key),
                        payload.size());
            }
            else {
                uint64_t length = 0;
                for (auto &&fragment : fragments) {
                    length += fragment.message.size();
                }

                payload = temporary_buffer<char>(length);

                uint64_t k = 0;
                char *buf = payload.get_write();
                for (unsigned int j = 0; j < fragments.size(); ++j) {
                    un_mask(buf + k, fragments[j].message.get(), (char *) (&fragments[j].mask_key), fragments.size());
                    k += fragments.size();
                }
            }

            if (opcode == websocket_opcode::TEXT && !utf8_check((const unsigned char *) payload.get(), payload.size())) {
                throw std::exception();
            }
        }

        void done() {
            _header[1] = write_payload_size();
        };
    };
}

#endif
