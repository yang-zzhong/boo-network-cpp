#include "http_server.hpp"
#include "http_message.hpp"
#include "http_client.hpp"
#include "ws_client.hpp"
#include "3rd/mongoose.h"
#include "routing.hpp"
#include <functional>
#include <iostream>
#include <map>
#include "3rd/json.hpp"

using boo::network::http_server;
using boo::network::routing;
using boo::network::http_request;
using boo::network::http_response;
using boo::network::http_client;
using boo::network::ws_client;
using nlohmann::json;

static mg_serve_http_opts opts;

http_server s;
routing::router<std::function<void(http_server::http_context *, routing::params * p)>> http_router;
routing::router<std::function<void(http_server::ws_conn *, const json &)>> ws_router;

void start_server(int port) {
    http_router.on(routing::get, "/hello-world", [](http_server::http_context * ctx, routing::params * p) {
        auto req = ctx->req();
        printf("an\\\\\\\\\\\\\\\\\\\\\\\\\---\---\n");
        printf("body: %s", req->body.c_str());
        std::map<std::string, std::string> headers{
            {"hello-world", "hello-world"},
        };
        ctx->send(200, "hello world", &headers);
    });
    http_router.on(routing::get, "/ws/{id}", [](http_server::http_context * ctx, routing::params * p) {
        if (!ctx->is_websocket_handshake_done()) {
            return;
        }
        http_server::ws_conn wctx{ ctx->conn() };
        nlohmann::json data;
        data["id"] = "hello-world";
        data["method"] = "POST";
        wctx.send(data);
    });
    ws_router.on(routing::post, "hello-world", [](http_server::ws_conn * conn, const json & data) {
        printf("server receive ws hello-world\n");
        json msg;
        msg["id"] = "hello-world";
        msg["data"] = "hello world";
        conn->send(msg);
    });
    s.enable_http_api(&http_router);
    s.enable_ws(&ws_router);
    s.set_on_ws_close([](const http_server::ws_conn &) {
    
    });
    opts.document_root = "d:\\projects\\vod-service-3.0\\test\\html";
    s.enable_webroot(&opts);
    s.listen(port);

    std::thread([&]() {
        s.poll(1000);
    }).detach();
}

void send_http(const std::string & base, const http_request & req) {
    http_client client(base);
    // if (!client.connect()) {
    //     std::cout << "connect error" << std::endl;
    //     return;
    // }
    http_response hm;
    client.send(req, hm);
    std::cout << hm.body << std::endl;
    client.disconnect();
}

void send_ws() {
    routing::router<std::function<void(ws_client*, const json &)>> client_ws_router;
    client_ws_router.on("/hello-world", [](ws_client * c, const json & msg) {
        printf("client receive hello world\n");
        printf(msg["data"].get<std::string>().c_str());
    });
    ws_client c(&client_ws_router);
    c.reconnect_when_closed();
    if (!c.connect("ws://127.0.0.1:8800/ws/A01?client_type=control&client_source=vod")) {
        std::cout << "connect error" << std::endl;
    }
    // json msg;
    // msg["id"] = "/hello-world";
    // msg["method"] = "POST";
    // c.send(msg);
    for(;;) {}
}

int main() {
    start_server(8111);
    http_request req("/hello-world", "POST");
    auto base = "127.0.0.1:8800";
    http_client::send(base, req, [](const http_response & res) {
        std::cout << res.body << std::endl;
    });
    http_response res;
    http_client::send(base, req, res);
    std::cout << res.body << std::endl;

    return 0;
}