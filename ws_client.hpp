#pragma once

#include "3rd/mongoose.h"
#include "routing.hpp"
#include "3rd/json.hpp"
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
    bool _connecting = false;
    bool _stopped = false;
    bool _handshake_done = false;
    bool _reconnect_when_closed = false;

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

    routing::router<std::function<void(ws_client*, const json &)>> * router()
    {
        return _router;
    }

    void reconnect_when_closed(bool reconnect = true)
    {
        _reconnect_when_closed = reconnect;
    }

    bool connect()
    {
        if (_url == "") {
            return false;
        }
        if (_connecting || _connected) {
            return true;
        }
        if (!do_connect()) {
            return false;
        }
        std::thread([&]() {
            while (!_stopped) {
                mg_mgr_poll(&_mgr, _poll_interval);
            }
        }).detach();
        while (_connecting) { }

        return _connected;
    }

    bool connect(const std::string & url, int poll_interval = 10)
    {
        _url = url;
        _poll_interval = poll_interval;
        return connect();
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
        _stopped = true;
        _connected = false;
    }
private:
    bool do_connect()
    {
        _connecting = false;
        mg_mgr_init(&_mgr, nullptr);
        _nc = mg_connect_ws(&_mgr, ws_client::mongoose_ev_handler, _url.c_str(), "websocket", NULL);
        if (_nc == nullptr) {
            return false;
        }
        _nc->user_data = this;
        _connecting = true;
        return true;
    }

    void reconnect()
    {
        if (_stopped) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        mg_mgr_free(&_mgr);
        if (!do_connect()) {
            mg_mgr_free(&_mgr);
        }
    }

    static void mongoose_ev_handler(struct mg_connection *nc, int ev, void *ev_data)
    {
        auto client = (ws_client*) nc->user_data;
        switch (ev) {
            case MG_EV_CONNECT: {
                int status = *((int *) ev_data);
                if (status != 0) {
                    client->_connected = false;
                } else {
                    client->_connected = true;
                }
                client->_connecting = false;
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
            case MG_EV_WEBSOCKET_FRAME: {
                struct websocket_message *wm = (struct websocket_message *) ev_data;
                std::string msg((const char *)wm->data, wm->size);
                client->route(json::parse(msg));
                break;
            }
            case MG_EV_CLOSE:
                client->_connected = false;
                if (client->_reconnect_when_closed) {
                    client->reconnect();
                }
                break;
        }
    }
};

}}