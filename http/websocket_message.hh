#include "websocket_fragment.hh"

#ifndef SEASTAR_WEBSOCKET_MESSAGE_HH
#define SEASTAR_WEBSOCKET_MESSAGE_HH

namespace httpd {
    class websocket_message_base {
    public:
        websocket_opcode opcode = RESERVED;
        uint8_t _header_size = 0;
        temporary_buffer<char> payload;
        bool fin = true;

        websocket_message_base(websocket_opcode opcode, temporary_buffer<char> payload) noexcept :
                opcode(opcode), payload(std::move(payload)) {
        };

        websocket_message_base(websocket_opcode opcode, sstring message, bool fin = true) noexcept :
                opcode(opcode), payload(std::move(std::move(message).release())), fin(fin) {
        };

        websocket_message_base() noexcept {};

        websocket_message_base(const websocket_message_base &) = delete;

        websocket_message_base(websocket_message_base &&other) noexcept : opcode(other.opcode),
                                                                          _header_size(other._header_size),
                                                                          payload(std::move(other.payload)),
                                                                          fin(other.fin) {
        }

        void operator=(const websocket_message_base &) = delete;

        websocket_message_base &operator=(websocket_message_base &&other) noexcept {
            if (this != &other) {
                opcode = other.opcode;
                _header_size = other._header_size;
                payload = std::move(other.payload);
                fin = other.fin;
            }
            return *this;
        }

        explicit operator bool() const { return !payload.empty(); }

        uint8_t write_payload_size(char* header);
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
                websocket_message_base(fragments.back().header.opcode, temporary_buffer<char>(
                        std::accumulate(fragments.begin(), fragments.end(), 0,
                                        [] (size_t x, inbound_websocket_fragment<CLIENT>& y) {
                                            return x + y.message.size();
                                        }))) {
            uint64_t k = 0;
            char* buf = payload.get_write();
            for (unsigned int j = 0; j < fragments.size(); ++j) {
                std::memcpy(buf + k, fragments[j].message.get(), fragments.size());
                k += fragments.size();
            }
            if (opcode == websocket_opcode::TEXT &&
                !utf8_check((const unsigned char *) buf, payload.size())) {
                throw std::exception();
            }
        }

        websocket_message(inbound_websocket_fragment<CLIENT> & fragment) noexcept:
                websocket_message_base(fragment.header.opcode, std::move(fragment.message)) { }

        temporary_buffer<char> get_header() {
            temporary_buffer<char> header(14);
            auto wr = header.get_write();

            wr[1] = (char) (128 | write_payload_size(wr));
            //FIXME Constructing an independent_bits_engine is expensive. static thread_local ?
            static thread_local std::independent_bits_engine<std::default_random_engine, std::numeric_limits<uint32_t>::digits, uint32_t> rbe;
            uint32_t mask = rbe();
            std::memcpy(wr + _header_size, &mask, sizeof(uint32_t));
            _header_size += sizeof(uint32_t);
            un_mask(payload.get_write(), payload.get(), (char *) (&mask), payload.size());
            header.trim(_header_size);
            return std::move(header);
        }
    };

    template<>
    class websocket_message<SERVER> final : public websocket_message_base {
    public:
        using websocket_message_base::websocket_message_base;

        websocket_message() noexcept {}

        websocket_message(std::vector<inbound_websocket_fragment<SERVER>>& fragments):
                websocket_message_base(fragments.back().header.opcode, temporary_buffer<char>(
                        std::accumulate(fragments.begin(), fragments.end(), 0,
                                        [] (size_t x, inbound_websocket_fragment<SERVER>& y) {
                                            return x + y.message.size();
                                        }))) {
            uint64_t k = 0;
            char* buf = payload.get_write();
            for (unsigned int j = 0; j < fragments.size(); ++j) {
                un_mask(buf + k, fragments[j].message.get(), (char *) (&fragments[j].header.mask_key), fragments.size());
                k += fragments.size();
            }
            if (opcode == websocket_opcode::TEXT && !utf8_check((const unsigned char *) buf, payload.size())) {
                throw std::exception();
            }
        }

        websocket_message(inbound_websocket_fragment<SERVER> & fragment) noexcept:
                websocket_message_base(fragment.header.opcode, temporary_buffer<char>(fragment.message.size())) {
            un_mask(payload.get_write(), fragment.message.get(), (char *) (&fragment.header.mask_key), payload.size());
        }

        temporary_buffer<char> get_header() {
            temporary_buffer<char> header(14);
            auto wr = header.get_write();

            wr[1] = write_payload_size(wr);
            header.trim(_header_size);

            return std::move(header);
        };
    };
}

#endif
