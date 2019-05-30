#pragma once

#include "3rd/mongoose.h"
#include "http_message.hpp"
#include <string>
#include <map>
#include <functional>
#include <thread>
#include <vector>
#include <list>
#include <mutex>

namespace boo { namespace network {

class http_client
{
    mg_mgr _mgr;
    struct mg_connection * _nc = nullptr;
    bool _handled = false;
    std::function<void(const http_response &)> _handler;
    std::string _base;
    bool _connected = false;
    bool _connecting = false;
    std::mutex _lock;

    std::list<std::pair<http_request, std::function<void(const http_response &)>>> _requests;

public:
    http_client()
    {
    }

    http_client(const std::string & base): _base(base)
    {
    }

    ~http_client() {
        if (_connected) {
            _connected = false;
        }
    }

    bool connect(const std::string & base, size_t poll_interval = 10)
    {
        _base = base;
        return connect(poll_interval);
    }

    bool connect(size_t poll_interval = 10)
    {
        std::lock_guard<std::mutex> locker(_lock);
        if (_base == "") {
            throw "没有设置base";
        }
        mg_mgr_init(&_mgr, NULL);
        _nc = mg_connect(&_mgr, _base.c_str(), http_client::mongoose_http_ev_handler);
        mg_set_protocol_http_websocket(_nc);
        _nc->user_data = this;
        _connecting = true;
        std::thread([&, poll_interval]() {
            while (_connecting || _connected) {
                mg_mgr_poll(&_mgr, poll_interval);
            }
            mg_mgr_free(&_mgr);
        }).detach();
        while (_connecting) { }
        if (_connected) {
            std::thread([&] {
                while (_connected) {
                    _lock.lock();
                    auto i = _requests.begin();
                    while (i != _requests.end()) {
                        send_and_handle_reply(i->first, i->second);
                        _requests.erase(i);
                        i = _requests.begin();
                    }
                    _lock.unlock();
                }
            }).detach();
        }

        return _connected;
    }

    void send_and_handle_reply(const http_request & req, std::function<void(const http_response &)> handler)
    {
        do_send(req);
        _handled = false;
        _handler = handler;
        while (!_handled) {}
    }

    void do_send(const http_request & req)
    {
        std::string msg = req.method + " " + req.target.str() + " HTTP/1.1\r\n";
        for (auto i = req.headers.begin(); i != req.headers.end(); ++i) {
            msg += i->first + ":" + i->second + "\r\n";
        }
        msg += "Host:" + _base + "\r\n";
        msg += "Content-Length: " + std::to_string(req.body.length()) + "\r\n";
        msg += "\r\n";
        msg += req.body + "\r\n";

        mg_printf(_nc, "%s", msg.c_str());
    }

    void send(const http_request & req, http_response & res)
    {
        const http_response * rp = nullptr;
        send(req, [&](const http_response & r) {
            rp = &r;
        });
        while (rp == nullptr) { }

        res = *rp;
    }

    void send(const http_request & req, std::function<void(const http_response & )> handler)
    {
        std::lock_guard<std::mutex> locker(_lock);
        _requests.push_back(std::pair<http_request, std::function<void(const http_response &)>>(req, handler));
    }

    static void mongoose_http_ev_handler(struct mg_connection *nc, int ev, void *ev_data)
    {
        auto client = (http_client *)nc->user_data;
        struct http_message *hm = (struct http_message *)ev_data;
        switch (ev) {
            case MG_EV_CONNECT:
                if (*(int *) ev_data != 0) {
                    std::lock_guard<std::mutex> locker(client->_lock);
                    client->_connected = false;
                } else {
                    client->_connected = true;
                }
                client->_connecting = false;
                break;
            case MG_EV_HTTP_REPLY:
                client->_handler(http_response::from_hm(hm));
                client->_handled = true;
                break;
        }
    }
};

} }