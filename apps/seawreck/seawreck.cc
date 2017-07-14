/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 */

#include "http/http_response_parser.hh"
#include "http/websocket.hh"
#include "core/print.hh"
#include "core/reactor.hh"
#include "core/app-template.hh"
#include "core/future-util.hh"
#include "core/distributed.hh"
#include "core/semaphore.hh"
#include "core/future-util.hh"
#include <chrono>
#include <climits>

using namespace seastar;

template <typename... Args>
void http_debug(const char* fmt, Args&&... args) {
#if HTTP_DEBUG
    print(fmt, std::forward<Args>(args)...);
#endif
}

class http_client {

private:
    unsigned _duration;
    unsigned _conn_per_core;
    unsigned _reqs_per_conn;
    std::vector<connected_socket> _sockets;
    semaphore _conn_connected{0};
    semaphore _conn_finished{0};
    timer<> _run_timer;
    bool _timer_based;
    bool _timer_done{false};
    uint64_t _total_reqs{0};
    bool _websocket{false};
    long _max_latency{0};
    long _min_latency{INT_MAX};
    double _sum_avr_latency{0};
    unsigned _payload_size;

public:
    http_client(unsigned duration, unsigned total_conn, unsigned reqs_per_conn, bool websocket, unsigned payload_size)
            : _duration(duration)
            , _conn_per_core(total_conn / smp::count)
            , _reqs_per_conn(reqs_per_conn)
            , _run_timer([this] { _timer_done = true; })
            , _timer_based(reqs_per_conn == 0)
            , _websocket(websocket)
            , _payload_size(payload_size) {
    }

    class connection {
    private:
        connected_socket _fd;
        input_stream<char> _read_buf;
        output_stream<char> _write_buf;
        http_response_parser _parser;
        http_client* _http_client;
        uint64_t _nr_done{0};
        long _max_latency{0};
        long _min_latency{INT_MAX};
        long _sum_latency{0};

    public:
        connection(connected_socket&& fd, http_client* client)
                : _fd(std::move(fd))
                , _read_buf(_fd.input())
                , _write_buf(_fd.output())
                , _http_client(client){
        }

        uint64_t nr_done() {
            return _nr_done;
        }

        long max_latency() {
            return _max_latency;
        }

        long min_latency() {
            return _min_latency;
        }

        double avr_latency() {
            return (double)_sum_latency / (double)_nr_done;
        }

        future<> do_req() {
            auto start = std::chrono::steady_clock::now();
            return _write_buf.write("GET / HTTP/1.1\r\nHost: 127.0.0.1:10000\r\n\r\n").then([this] {
                return _write_buf.flush();
            }).then([this, start] {
                _parser.init();
                return _read_buf.consume(_parser).then([this, start] {
                    // Read HTTP response header first
                    if (_parser.eof()) {
                        return make_ready_future<>();
                    }
                    auto _rsp = _parser.get_parsed_response();
                    auto it = _rsp->_headers.find("Content-Length");
                    if (it == _rsp->_headers.end()) {
                        print("Error: HTTP response does not contain: Content-Length\n");
                        return make_ready_future<>();
                    }
                    auto content_len = std::stoi(it->second);
                    http_debug("Content-Length = %d\n", content_len);
                    // Read HTTP response body
                    return _read_buf.read_exactly(content_len).then([this, start] (temporary_buffer<char> buf) {
                        _nr_done++;
                        auto ping = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
                        _sum_latency += ping;
                        if (ping > _max_latency)
                            _max_latency = ping;
                        if (ping < _min_latency)
                            _min_latency = ping;
                        http_debug("%s\n", buf.get());
                        if (_http_client->done(_nr_done)) {
                            return make_ready_future();
                        } else {
                            return do_req();
                        }
                    });
                });
            });
        }
    };

    class ws_connection {
    private:
        connected_socket _fd;
        input_stream<char> _read_buf;
        output_stream<char> _write_buf;
        http_client* _http_client;
        uint64_t _nr_done{0};
        long _max_latency{0};
        long _min_latency{INT_MAX};
        long _sum_latency{0};
        httpd::websocket::message<httpd::websocket::endpoint_type::CLIENT> _message;
        temporary_buffer<char> _header;
        long _read_value;
    public:
        ws_connection(connected_socket&& fd, http_client* client)
                : _fd(std::move(fd))
                , _read_buf(_fd.input())
                , _write_buf(_fd.output())
                , _http_client(client) {
            using random_bytes_engine = std::independent_bits_engine<
                    std::default_random_engine, std::numeric_limits<unsigned char>::digits, unsigned char>;
            random_bytes_engine rbe;

            sstring payload(client->_payload_size, '\0');

            std::generate(payload.begin(), payload.end(), std::ref(rbe));
            _message = httpd::websocket::message<httpd::websocket::endpoint_type::CLIENT>(httpd::websocket::opcode::BINARY, payload);
            _header = _message.get_header();
            _read_value = _header.size() - 4 + _message.payload.size();
        }

        uint64_t nr_done() {
            return _nr_done;
        }

        long max_latency() {
            return _max_latency;
        }

        long min_latency() {
            return _min_latency;
        }

        double avr_latency() {
            return (double)_sum_latency / (double)_nr_done;
        }

        future<> do_req() {

            auto start = std::chrono::steady_clock::now();
            return _write_buf.write(temporary_buffer<char>(_header.begin(), _header.size())).then([this] {
                _write_buf.write(temporary_buffer<char>(_message.payload.begin(), _message.payload.size()));
            }).then([this] { return _write_buf.flush(); }).then([this, start] {
                return _read_buf.skip(_read_value).then([this, start] {
                    auto ping = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
                    _sum_latency += ping;
                    if (ping > _max_latency)
                        _max_latency = ping;
                    if (ping < _min_latency)
                        _min_latency = ping;
                    _nr_done++;
                    if (_http_client->done(_nr_done)) {
                        return make_ready_future();
                    } else {
                        return do_req();
                    }
                });
            });
        }
    };

    future<uint64_t> total_reqs() {
        print("Requests on cpu %2d: %ld\n", engine().cpu_id(), _total_reqs);
        return make_ready_future<uint64_t>(_total_reqs);
    }

    long max_latency() {
        return _max_latency;
    }

    long min_latency() {
        return _min_latency;
    }

    double avr_latency() {
        return _sum_avr_latency / (double)_conn_per_core;
    }

    bool done(uint64_t nr_done) {
        if (_timer_based) {
            return _timer_done;
        } else {
            return nr_done >= _reqs_per_conn;
        }
    }

    future<> connect(ipv4_addr server_addr) {
        // Establish all the TCP connections first
        if (_websocket) {
            for (unsigned i = 0; i < _conn_per_core; i++) {
                httpd::websocket::connect(make_ipv4_address(server_addr)).then(
                        [this](connected_socket fd) {
                            _sockets.push_back(std::move(fd));
                            http_debug("Established connection %6d on cpu %3d\n", _conn_connected.current(),
                                       engine().cpu_id());
                            _conn_connected.signal();
                        }).or_terminate();
            }
        }
        else {
            for (unsigned i = 0; i < _conn_per_core; i++) {
                engine().net().connect(make_ipv4_address(server_addr)).then([this] (connected_socket fd) {
                    _sockets.push_back(std::move(fd));
                    http_debug("Established connection %6d on cpu %3d\n", _conn_connected.current(), engine().cpu_id());
                    _conn_connected.signal();
                }).or_terminate();
            }
        }
        return _conn_connected.wait(_conn_per_core);
    }

    future<> run() {
        // All connected, start HTTP request
        http_debug("Established all %6d tcp connections on cpu %3d\n", _conn_per_core, engine().cpu_id());
        if (_timer_based) {
            _run_timer.arm(std::chrono::seconds(_duration));
        }

        if (_websocket) {
            for (auto&& fd : _sockets) {
                auto conn = new ws_connection(std::move(fd), this);
                conn->do_req().then_wrapped([this, conn] (auto&& f) {
                    http_debug("Finished connection %6d on cpu %3d\n", _conn_finished.current(), engine().cpu_id());
                    _total_reqs += conn->nr_done();
                    _sum_avr_latency += conn->avr_latency();
                    if (conn->max_latency() > _max_latency)
                        _max_latency = conn->max_latency();
                    if (conn->min_latency() < _min_latency)
                        _min_latency = conn->min_latency();
                    _conn_finished.signal();
                    delete conn;
                    try {
                        f.get();
                    } catch (std::exception& ex) {
                        print("websocket error: %s\n", ex.what());
                    }
                });
            }
        }
        else {
            for (auto&& fd : _sockets) {
                auto conn = new connection(std::move(fd), this);
                conn->do_req().then_wrapped([this, conn] (auto&& f) {
                    http_debug("Finished connection %6d on cpu %3d\n", _conn_finished.current(), engine().cpu_id());
                    _total_reqs += conn->nr_done();
                    _sum_avr_latency += conn->avr_latency();
                    if (conn->max_latency() > _max_latency)
                        _max_latency = conn->max_latency();
                    if (conn->min_latency() < _min_latency)
                        _min_latency = conn->min_latency();
                    _conn_finished.signal();
                    delete conn;
                    try {
                        f.get();
                    } catch (std::exception& ex) {
                        print("http request error: %s\n", ex.what());
                    }
                });
            }
        }

        // All finished
        return _conn_finished.wait(_conn_per_core);
    }
    future<> stop() {
        return make_ready_future();
    }
};

// Implements @Reducer concept.
template <typename Result, typename Addend = Result>
class max {
private:
    Result _result;
public:
    future<> operator()(const Addend& value) {
        if (value > _result)
            _result = value;
        return make_ready_future<>();
    }
    Result get() && {
        return std::move(_result);
    }
};

// Implements @Reducer concept.
template <typename Result, typename Addend = Result>
class min {
private:
    Result _result;
    bool init{false};
public:
    future<> operator()(const Addend& value) {
        if (value < _result || !init)
        {
            _result = value;
            init = true;
        }
        return make_ready_future<>();
    }
    Result get() && {
        return std::move(_result);
    }
};

namespace bpo = boost::program_options;

int main(int ac, char** av) {
    app_template app;
    app.add_options()
            ("server,s", bpo::value<std::string>()->default_value("127.0.0.1:10000"), "Server address")
            ("conn,c", bpo::value<unsigned>()->default_value(100), "total connections")
            ("reqs,r", bpo::value<unsigned>()->default_value(0), "reqs per connection")
            ("duration,d", bpo::value<unsigned>()->default_value(10), "duration of the test in seconds)")
            ("websocket,w", bpo::bool_switch()->default_value(false), "benchmark websocket")
            ("size,z", bpo::value<unsigned>()->default_value(20), "websocket request payload size");

    return app.run(ac, av, [&app] () -> future<int> {
        auto& config = app.configuration();
        auto server = config["server"].as<std::string>();
        auto reqs_per_conn = config["reqs"].as<unsigned>();
        auto total_conn= config["conn"].as<unsigned>();
        auto duration = config["duration"].as<unsigned>();
        auto websocket = config["websocket"].as<bool>();
        auto payload_size = config["size"].as<unsigned>();

        if (total_conn % smp::count != 0) {
            print("Error: conn needs to be n * cpu_nr\n");
            return make_ready_future<int>(-1);
        }

        auto http_clients = new distributed<http_client>;

        // Start http requests on all the cores
        auto started = steady_clock_type::now();
        print("========== http_client ============\n");
        print("Benchmark: %s\n", websocket ? "websocket" : "http");
        print("Server: %s\n", server);
        print("Connections: %u\n", total_conn);
        print("Requests/connection: %s\n", reqs_per_conn == 0 ? "dynamic (timer based)" : std::to_string(reqs_per_conn));
        return http_clients->start(std::move(duration), std::move(total_conn), std::move(reqs_per_conn), std::move
                (websocket), std::move(payload_size)).then([http_clients, started, server] {
            return http_clients->invoke_on_all(&http_client::connect, ipv4_addr{server});
        }).then([http_clients] {
            return http_clients->invoke_on_all(&http_client::run);
        }).then([http_clients] {
            return http_clients->map_reduce(adder<uint64_t>(), &http_client::total_reqs);
        }).then([http_clients, started] (auto total_reqs) {
            // All the http requests are finished
            auto finished = steady_clock_type::now();
            auto elapsed = finished - started;
            auto secs = elapsed.count() / 1000000000.0;
            print("Total cpus: %u\n", smp::count);
            print("Total requests: %u\n", total_reqs);
            print("Total time: %f\n", secs);
            print("Requests/sec: %f\n", static_cast<double>(total_reqs) / secs);
        }).then([http_clients] {
            return when_all(http_clients->map_reduce(max<long>(), &http_client::max_latency),
                            http_clients->map_reduce(min<long>(), &http_client::min_latency),
                            http_clients->map_reduce(adder<double>(), &http_client::avr_latency));
        }).then([http_clients] (std::tuple<future<long>, future<long>, future<double>> joined) {
            print("Max latency : %u ms\n", std::get<0>(joined).get0());
            print("Min latency : %u ms\n", std::get<1>(joined).get0());
            print("Avr latency : %f ms\n", std::get<2>(joined).get0() / (double)smp::count);
        }).then([http_clients] {
            print("==========     done     ============\n");
            return http_clients->stop().then([http_clients] {
                // FIXME: If we call engine().exit(0) here to exit when
                // requests are done. The tcp connection will not be closed
                // properly, becasue we exit too earily and the FIN packets are
                // not exchanged.
                delete http_clients;
                return make_ready_future<int>(0);
            });
        });
    });
}