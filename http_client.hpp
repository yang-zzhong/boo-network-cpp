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
    std::list<http_request> _send_queues;
    std::string _base;
    bool _connected = false;
    bool _connecting = false;
    bool _stopped = true;
    bool _mbr_valid = false;
    std::mutex _send_lock;
    std::mutex _lock;
    std::mutex _handle_lock;

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
        if (_connecting || _connected) {
            return true;
        }
        if (!_mbr_valid) {
            mg_mgr_init(&_mgr, NULL);
            _mbr_valid = true;
        }
        _nc = mg_connect(&_mgr, _base.c_str(), http_client::mongoose_http_ev_handler);
        if (_nc == nullptr) {
            return false;
        }
        mg_set_protocol_http_websocket(_nc);
        _nc->user_data = this;
        _connecting = true;
        std::thread([&, poll_interval]() {
            while (_connecting || _connected) {
                mg_mgr_poll(&_mgr, poll_interval);
            }
            mg_mgr_free(&_mgr);
            _mbr_valid = false;
        }).detach();
        while (_connecting) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (_connected) {
            std::thread([&] {
                _stopped = false;
                while (_connected) {
                    _lock.lock();
                    auto i = _requests.begin();
                    while (i != _requests.end()) {
                        send_and_handle_reply(i->first, i->second);
                        _requests.erase(i);
                        i = _requests.begin();
                    }
                    _lock.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                _stopped = true;
            }).detach();
        }

        return _connected;
    }

    void disconnect()
    {
        if (_connected) {
            _connected = false;
            _nc->flags |= MG_F_CLOSE_IMMEDIATELY;
        }
        while(!_stopped || _mbr_valid) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }


    bool send(const http_request & req, http_response & res)
    {
        bool found = false;
        auto queued = send(req, [&res, &found](const http_response & r) {
            res = http_response(r);
            found = true;
        });
        if (queued) {
            while (!found) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        return queued;
    }

    bool send(const http_request & req, std::function<void(const http_response & )> handler)
    {
        if (!_connected) {
            disconnect();
            connect();
        }
        if (_connected) {
            std::lock_guard<std::mutex> locker(_lock);
            _requests.push_back(std::pair<http_request, std::function<void(const http_response &)>>(req, handler));
        }
        return _connected;
    }

    static bool send(const std::string & base, const http_request & req, std::function<void(const http_response &)> handler)
    {
        auto client = new http_client(base);
        return client->send(req, [client, handler](const http_response & res) {
            handler(res);
            client->disconnect();
            delete client;
        });
    }

    static bool send(const std::string & base, const http_request & req, http_response & res)
    {
        http_client client(base);
        if (!client.send(req, res)) {
            client.disconnect();
            return false;
        }
        client.disconnect();
        return true;
    }

private:
    static void mongoose_http_ev_handler(struct mg_connection *nc, int ev, void *ev_data)
    {
        auto client = (http_client *)nc->user_data;
        client->handle_send_queue();
        struct http_message *hm = (struct http_message *)ev_data;
        switch (ev) {
            case MG_EV_CONNECT: {
                int code = *(int *)ev_data;
                if (code != 0) {
                    client->_connected = false;
                } else {
                    client->_connected = true;
                }
                client->_connecting = false;
            }
            break;
            case MG_EV_HTTP_REPLY:
                client->_handle_lock.lock();
                client->_handler(http_response::from_hm(hm));
                client->_handled = true;
                client->_handle_lock.unlock();
                break;
            case MG_EV_CLOSE:
                client->_lock.lock();
                client->_connected = false;
                client->_lock.unlock();
                break;
        }
    }

    void send_and_handle_reply(const http_request & req, std::function<void(const http_response &)> handler)
    {
        push_to_send(req);
        _handle_lock.lock();
        _handled = false;
        _handler = handler;
        _handle_lock.unlock();
        while (!_handled) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void push_to_send(const http_request & req)
    {
        _send_lock.lock();
        _send_queues.push_back(req);
        _send_lock.unlock();
    }

    void handle_send_queue()
    {
        _send_lock.lock();
        for (auto i = _send_queues.begin(); i != _send_queues.end(); ++i) {
            do_send(*i);
        }
        _send_queues.clear();
        _send_lock.unlock();
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
};

} }