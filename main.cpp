#include "http_server.hpp"
#include "http_client.hpp"
#include "ws_client.hpp"
#include "mongoose.h"
#include "routing.hpp"
#include <functional>
#include <iostream>
#include <map>
#include "json.hpp"

using boo::network::http_server;
using boo::network::routing;
using boo::network::http_client;
using boo::network::ws_client;
using nlohmann::json;

static mg_serve_http_opts opts;

int main() {
    routing::router<std::function<void(http_server::http_context *, routing::params * p)>> http_router;
    http_router.on(routing::get, "/hello-world", [](http_server::http_context * ctx, routing::params * p) {
        auto req = ctx->req();
        std::string body(req->body.p, req->body.len);
        printf("an\\\\\\\\\\\\\\\\\\\\\\\\\---\---\n");
        std::map<std::string, std::string> headers{
            {"hello-world", "hello-world"},
        };
        ctx->send(200, "hello world", &headers);
    });
    routing::router<std::function<void(http_server::ws_context *)>> ws_router;
    ws_router.on(routing::post, "hello-world", [](http_server::ws_context * ctx) {
        printf("server receive ws hello-world\n");
        json msg;
        msg["id"] = "hello-world";
        msg["data"] = "hello world";
        ctx->send(msg);
    });
    http_server s;
    s.enable_http_api(&http_router);
    s.enable_ws(&ws_router);
    s.set_on_ws([](http_server::http_context * ctx) {
        http_server::ws_context wctx{ ctx->conn() };
        nlohmann::json data;
        data["id"] = "hello-world";
        data["method"] = "POST";
        wctx.send(data);
    });
    s.set_on_ws_close([](struct mg_connection * conn) {
    
    });
    opts.document_root = "d:\\projects\\vod-service-3.0\\test\\html";
    s.enable_webroot(&opts);
    s.listen(8080);
    std::thread([&]() {
        s.poll(1000);
    }).detach();

    http_client client("127.0.0.1:8080");
    client.connect();
    http_client::request req("hello-world", "GET");
    req.headers["hello-world"] = "hello world";
    req.body = "hello world";

    for (int i = 0; i < 10; ++i) {
        client.send(req, [](const struct http_message & hm) {
            std::string body(hm.body.p, hm.body.len);
            printf("reply: %s\n", body.c_str());
        });
    }

    routing::router<std::function<void(ws_client*, const json &)>> client_ws_router;
    client_ws_router.on("/hello-world", [](ws_client * c, const json & msg) {
        printf("client receive hello world\n");
        printf(msg["data"].get<std::string>().c_str());
    });
    ws_client c(&client_ws_router);
    c.connect("ws://127.0.0.1:8080/ws/A01?client_type=control&client_source=vod");
    json msg;
    msg["id"] = "/hello-world";
    msg["method"] = "POST";
    c.send(msg);
    for(;;) {}
}