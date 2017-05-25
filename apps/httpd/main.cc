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
 * Copyright 2015 Cloudius Systems
 */

#include <cryptopp/sha.h>
#include <cryptopp/base64.h>
#include "http/httpd.hh"
#include "http/handlers.hh"
#include "http/websocket_handler.hh"
#include "http/function_handlers.hh"
#include "http/file_handler.hh"
#include "apps/httpd/demo.json.hh"
#include "http/api_docs.hh"

namespace bpo = boost::program_options;

using namespace seastar;
using namespace httpd;
using namespace websocket;

class handl : public httpd::handler_base {
public:
    virtual future<std::unique_ptr<reply> > handle(const sstring& path,
            std::unique_ptr<request> req, std::unique_ptr<reply> rep) {
        rep->_content = "hello";
        rep->done("html");
        return make_ready_future<std::unique_ptr<reply>>(std::move(rep));
    }
};

void set_routes(routes& r) {
    function_handler* h1 = new function_handler([](const_req req) {
        return "hello";
    });
    function_handler* h2 = new function_handler([](std::unique_ptr<request> req) {
        return make_ready_future<json::json_return_type>("json-future");
    });

    auto ws_echo_handler = new websocket::ws_handler<SERVER>();

    ws_echo_handler->on_message_future([] (const std::unique_ptr<request>& req, duplex_stream<SERVER>& stream,
            message<SERVER> message) {
        return stream.write(std::move(message)).then([&stream] {
            return stream.flush();
        });
    });

    auto ws_sha1_handler = new websocket::ws_handler<SERVER>();

    ws_sha1_handler->on_connection_future([] (const std::unique_ptr<request>& req, duplex_stream<SERVER>& stream) {
        return stream.write(websocket::message<SERVER>(TEXT, "Hello from seastar ! Send any message to this endpoint"
                " and get back it's payload SHA1 in base64 format !")).then([&stream] {
            return stream.flush();
        });
    });

    ws_sha1_handler->on_message_future([] (const std::unique_ptr<request>& req, duplex_stream<SERVER>& stream,
            message<SERVER> message) {
        CryptoPP::SHA hash;
        byte digest[CryptoPP::SHA::DIGESTSIZE];
        hash.Update((byte*) message.payload.begin(), message.payload.size());
        hash.Final(digest);

        sstring base64;

        CryptoPP::Base64Encoder encoder;
        encoder.Put(digest, sizeof(digest));
        encoder.MessageEnd();
        CryptoPP::word64 size = encoder.MaxRetrievable();
        if (size) {
            base64.resize(size);
            encoder.Get((byte*) base64.data(), base64.size());

            return stream.write(websocket::message<SERVER>(TEXT, base64.substr(0, base64.size() - 1))).then([&stream] {
                return stream.flush();
            });
        }
        return make_ready_future();
    });

    ws_sha1_handler->on_disconnection([] (const std::unique_ptr<request>& req) {
        print("websocket client disconnected\n");
    });

    r.add(operation_type::GET, url("/"), h1);
    r.add(operation_type::GET, url("/jf"), h2);
    r.add(operation_type::GET, url("/file").remainder("path"), new directory_handler("/"));
    r.put("/", ws_echo_handler);
    r.put("/sha1", ws_sha1_handler);

    demo_json::hello_world.set(r, [] (const_req req) {
        demo_json::my_object obj;
        obj.var1 = req.param.at("var1");
        obj.var2 = req.param.at("var2");
        demo_json::ns_hello_world::query_enum v = demo_json::ns_hello_world::str2query_enum(req.query_parameters.at("query_enum"));
        // This demonstrate enum conversion
        obj.enum_var = v;
        return obj;
    });
}

int main(int ac, char** av) {
    app_template app;
    app.add_options()("port", bpo::value<uint16_t>()->default_value(10000),
                      "HTTP Server port");
    return app.run_deprecated(ac, av, [&] {
        auto &&config = app.configuration();
        uint16_t port = config["port"].as<uint16_t>();
        auto server = new http_server_control();
        auto rb = make_shared<api_registry_builder>("apps/httpd/");
        server->start().then([server] {
            return server->set_routes(set_routes);
        }).then([server, rb] {
            return server->set_routes([rb](routes &r) { rb->set_api_doc(r); });
        }).then([server, rb] {
            return server->set_routes([rb](routes &r) { rb->register_function(r, "demo", "hello world application"); });
        }).then([server, port] {
            return server->listen(port);
        }).then([server, port] {
            std::cout << "Seastar HTTP server listening on port " << port << " ...\n";
            engine().at_exit([server] {
                return server->stop();
            });
        });
    });
}
