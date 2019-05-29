#pragma once

#include "mongoose.h"
#include "routing.hpp"
#include "json.hpp"
#include <string>
#include <thread>
#include <sstream>

namespace boo { namespace network {

using boo::network::routing;
using nlohmann::json;

class ws_client 
{
    mg_mgr _mgr;
    mg_connection * _nc = nullptr;
    std::string _url;
    int _poll_interval;
    bool _connected = false;
    bool _mgr_freed = false;
    bool _handshake_done = false;

    routing::router<std::function<void(ws_client*, const json &)>> * _router;

    void route(const json & msg) {
        if (msg.find("id") == msg.end()) {
            return;
        }
        auto path = msg["id"].get<std::string>();
        if (msg.find("method") != msg.end()) {
            path = routing::concat_method_path(msg["method"].get<std::string>(), path);
        }
        routing::params p;
        _router->route(path, &p, [&](bool pathfound, routing::params * p, std::function<void(ws_client *, const json &)> callback) {
            if (pathfound && callback != nullptr) {
                callback(this, msg);
                return;
            }
        });
    }

public:
    ws_client(routing::router<std::function<void(ws_client *, const json &)>> * router) : _router(router) {} 

    void connect()
    {
        if (_url == "") {
            return;
        }
        if (_connected) {
            return;
        }
        while(_mgr_freed) {}
        mg_mgr_init(&_mgr, nullptr);
        _mgr_freed = false;
        _nc = mg_connect_ws(&_mgr, ws_client::mongoose_ev_handler, _url.c_str(), "websocket", NULL);
        _nc->user_data = this;
        _connected = true;
        std::thread([&]() {
            while (_connected) {
                mg_mgr_poll(&_mgr, _poll_interval);
            }
            mg_mgr_free(&_mgr);
            _mgr_freed = true;
        }).detach();
        while (!_handshake_done) { }
        _handshake_done = false;
    }

    void connect(const std::string & url, int poll_interval = 10)
    {
        _url = url;
        _poll_interval = poll_interval;
        connect();
    }

    void send(const json & data)
    {
        std::stringstream out;
        out << data;
        std::string msg = out.str();

        mg_send_websocket_frame(_nc, WEBSOCKET_OP_TEXT, msg.c_str(), msg.length());
    }

    ~ws_client()
    {
        _connected = false;
    }

    static void mongoose_ev_handler(struct mg_connection *nc, int ev, void *ev_data)
    {
        auto client = (ws_client*) nc->user_data;
        switch (ev) {
            case MG_EV_CONNECT: {
                int status = *((int *) ev_data);
                if (status != 0) {
                    client->_connected = false;
                    client->connect();
                }
                break;
            }
            case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
               client->_handshake_done = true;
               struct http_message *hm = (struct http_message *) ev_data;
               if (hm->resp_code == 101) {
                    client->_connected = true;
               }
               break;
            }
            case MG_EV_WEBSOCKET_FRAME: 
                struct websocket_message *wm = (struct websocket_message *) ev_data;
                std::string msg((const char *)wm->data, wm->size);
                client->route(json::parse(msg));
                break;
        }
    }
};

}}