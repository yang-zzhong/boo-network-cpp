#pragma once

#include "mongoose.h"
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
    std::function<void(const struct http_message &)> _handler;
    std::string _base;
    bool _connected = false;
    std::mutex _lock;

public:
    http_client(const std::string & base): _base(base)
    {
        mg_mgr_init(&_mgr, NULL);
    }

    ~http_client() {
        if (_connected) {
            _connected = false;
        }
    }

    class request {
    public:
        std::string url;
        std::string method;
        std::string body;
        std::map<std::string, std::string> headers;

        request(const std::string & u, const std::string & m): url(u), method(m) {}
    };

    std::list<std::pair<request, std::function<void(const struct http_message &)>>> _requests;

    void connect(size_t poll_interval = 10)
    {
        std::lock_guard<std::mutex> locker(_lock);
        _nc = mg_connect(&_mgr, _base.c_str(), http_client::mongoose_http_ev_handler);
        mg_set_protocol_http_websocket(_nc);
        _nc->user_data = this;
        _connected = true;
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
        std::thread([&, poll_interval]() {
            while (_connected) {
                mg_mgr_poll(&_mgr, poll_interval);
            }
            mg_mgr_free(&_mgr);
        }).detach();
    }

    void send_and_handle_reply(const request & req, std::function<void(const struct http_message &)> handler)
    {
        do_send(req);
        _handled = false;
        _handler = handler;
        while (!_handled) {}
    }

    void do_send(const request & req)
    {
        std::string msg = req.method + " " + req.url + " HTTP/1.1\r\n";
        for (auto i = req.headers.begin(); i != req.headers.end(); ++i) {
            msg += i->first + ":" + i->second + "\r\n";
        }
        msg += "Host:" + _base + "\r\n";
        msg += "Content-Length: " + std::to_string(req.body.length()) + "\r\n";
        msg += "\r\n";
        msg += req.body + "\r\n";

        mg_printf(_nc, "%s", msg.c_str());
    }

    void send(const request & req, struct http_message & res)
    {
        const struct http_message * rp = nullptr;
        send(req, [&](const struct http_message & r) {
            rp = &r;
        });
        while (rp == nullptr) { }

        res = *rp;
    }

    void send(const request & req, std::function<void(const struct http_message & )> handler)
    {
        std::lock_guard<std::mutex> locker(_lock);
        _requests.push_back(std::pair<request, std::function<void(const struct http_message &)>>(req, handler));
    }

    static void mongoose_http_ev_handler(struct mg_connection *nc, int ev, void *ev_data)
    {
        auto client = (http_client *)nc->user_data;
        struct http_message *hm = (struct http_message *)ev_data;
        switch (ev) {
            case MG_EV_CONNECT:
                if (*(int *) ev_data != 0) {
                    std::lock_guard<std::mutex> locker(client->_lock);
                    fprintf(stderr, "connect() failed: %s\n", strerror(*(int *) ev_data));
                    client->_connected = false;
                }
                break;
            case MG_EV_HTTP_REPLY:
                client->_handler(*hm);
                client->_handled = true;
                break;
        }
    }
};

} }