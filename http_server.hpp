#pragma once

#include "routing.hpp"
#include "3rd/json.hpp"
#include "3rd/mongoose.h"
#include "http_message.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include <functional>
#include <map>
#include <list>

using m_http_message = struct http_message;

namespace boo { namespace network {

using nlohmann::json;

enum conn_type {
    conn_type_ws,
    conn_type_http
};

class http_server {

    struct msg_t {
        mg_connection * nc;
        std::string str;
    };
    std::vector<msg_t> _for_send;
    std::mutex _m;
    std::function<void(conn_type t, const std::string & method, const string & path, const std::string & content)> _on_api;
    std::function<void(const std::string & msg)> _on_ws_sent;
public:

    void on_api(std::function<void(conn_type t, const std::string & m, const std::string & path, const std::string & content)> onapi)
    {
        _on_api = onapi;
    }

    void on_ws_sent(std::function<void(const std::string & content)> onsent)
    {
        _on_ws_sent = onsent;
    }

    void push_to_send(mg_connection * nc, const std::string & str)
    {
        _m.lock();
        _for_send.push_back(msg_t{nc, str});
        _m.unlock();
    }

    void send()
    {
        _m.lock();
        for (auto i = _for_send.begin(); i != _for_send.end(); ++i) {
            mg_send_websocket_frame(i->nc, WEBSOCKET_OP_TEXT, i->str.c_str(), i->str.length());
        }
        _for_send.clear();
        _m.unlock();
    }

class http_context {
    struct mg_connection * _nc;
    m_http_message * _hm;
    http_request * _req;
    http_server * _server = nullptr;
    bool _is_websocket_handshake_done = false;
public:
    http_context(struct mg_connection * nc, http_request * r, m_http_message * hm): _nc(nc), _req(r), _hm(hm)
    {
    }

    void set_server(http_server * s)
    {
        _server = s;
    }

    http_server * server()
    {
        return _server;
    }

    mg_connection * conn() {
        return _nc;
    }

    m_http_message * hm() {
        return _hm;
    }

    http_request * req() {
        return _req;
    }

    void close()
    {
    }

    void set_websocket_handshake_done(bool is)
    {
        _is_websocket_handshake_done = is;
    }

    bool is_websocket_handshake_done()
    {
        return _is_websocket_handshake_done;
    }

    void send(const char * buf, int size)
    {
        mg_send(_nc, buf, size);
    }

    void send(const http_response & res)
    {
        std::map<std::string, std::string> headers = res.headers;
        send(res.status_code, res.body, &headers);
    }

    void send(int status_code, char * body, std::map<std::string, std::string> * headers = nullptr)
    {
        send(status_code, std::string(body), headers);
    }

    void send(int status_code, const json & body, std::map<std::string, std::string> * headers = nullptr)
    {
        std::map<std::string, std::string> hs;
        if (headers != nullptr) {
            for (auto i = headers->begin(); i != headers->end(); ++i) {
                hs[i->first] = i->second;
            }
        }
        hs["Content-Type"] = "Application/json";

        std::stringstream out;
        out << body;

        send(status_code, out.str(), &hs);
    }

    void send_header(int status_code, std::map<std::string, std::string> * headers = nullptr)
    {
        return send_header(status_code, -1, headers);
    }

    void send_header(int status_code, int size, std::map<std::string, std::string> * headers = nullptr)
    {
        if (_nc == nullptr) {
            throw "has no connection";
        }
        std::string header;
        if (headers != nullptr) {
            unsigned int idx = 0;
            for (auto i = headers->begin(); i != headers->end(); ++i) {
                header += i->first + ": " + i->second;
                if (idx++ < headers->size() - 1) {
                    header += "\r\n";
                }
            }
        }
        mg_send_head(_nc, status_code, size, header.c_str());
    }

    void send_chunk(const char * buf, int len)
    {
        mg_send_http_chunk(_nc, buf, len);
    }

    void send_chunk_end()
    {
        mg_send_http_chunk(_nc, "", 0);
    }

    void send(int status_code, const std::string & body, std::map<std::string, std::string> * headers = nullptr)
    {
        send_header(status_code, headers);
        send_chunk(body.c_str(), body.length());
        send_chunk_end();
    }
};

class send_next_t {
public:
    virtual ~send_next_t(){}
    virtual void send(struct mg_connection * _nc) = 0;
    virtual bool send_complete() = 0;
    virtual void send_ok(struct mg_connection * _nc) = 0;
    virtual void close() = 0;
};


class ws_conn {
    struct mg_connection * _nc;
    http_server * _server;
    bool _valid_req = true;
public:
    json req;
    ws_conn(struct mg_connection * conn, http_server * s): _nc(conn), _server(s) {}

    bool is_valid() {
        return _valid_req;
    }

    void send(const json & data)
    {
        stringstream out;
        out << data;
        if (_server->_on_ws_sent != nullptr) {
            _server->_on_ws_sent(out.str());
        }
        _server->push_to_send(_nc, out.str());
    }

    bool eq(const ws_conn & ws) const
    {
        return _nc == ws._nc;
    }

    bool eq(const ws_conn * ws) const
    {
        return _nc == ws->_nc;
    }
};

private:
    mg_mgr _mgr;
    routing::router<function<void(ws_conn *, const json &)>> * _ws_router = nullptr;
    routing::router<function<void(http_context *, routing::params *)>> * _http_router = nullptr;

    struct mg_connection * _nc = nullptr;
    std::function<void(const ws_conn &)> _on_ws_close = nullptr;
    std::function<void(http_server * s, mg_connection * conn)> _on_http_close = nullptr;

    bool _stop = false;
    bool _stoped = false;
    bool _ws_enabled = false;
    bool _webroot_enabled = false;
    bool _http_api_enabled = false;
    bool _listening = false;

    std::map<struct mg_connection *, send_next_t *> _send_next;

    struct mg_serve_http_opts * _webroot_opts;

    static int is_websocket(const struct mg_connection *nc) {
        return nc->flags & MG_F_IS_WEBSOCKET;
    };

    void on_ws_close(const ws_conn & ws) {
        if (_on_ws_close != nullptr) {
            _on_ws_close(ws);
        }
    };

    void on_http_close(mg_connection * nc) {
        if (_send_next.find(nc) != _send_next.end()) {
            _send_next[nc]->close();
            delete _send_next[nc];
            _send_next.erase(nc);
        }
        if (_on_http_close != nullptr) {
            _on_http_close(this, nc);
        }
    }

public:
    http_server()
    {
    }

    void reg_send_next(struct mg_connection * nc, send_next_t * send_next)
    {
        if (_send_next.find(nc) != _send_next.end()) {
            delete _send_next[nc];
        }
        _send_next[nc] = send_next;
    }

    void stop() {
        _stop = true;
        while(_stoped) {}
    }

    void enable_ws(routing::router<function<void(ws_conn *, const json &)>> * router)
    {
        _ws_router = router;
        _ws_enabled = true;
    }

    void enable_webroot(struct mg_serve_http_opts * opts)
    {
        _webroot_opts = opts;
        _webroot_enabled = true;
    }

    void enable_http_api(routing::router<function<void(http_context *, routing::params *)>> * router)
    {
        _http_router = router;
        _http_api_enabled = true;
    }

    void handle_http_api(struct mg_connection * nc, struct http_message * hm, bool is_websocket = false)
    {
        if (!_http_api_enabled) {
            if (_webroot_enabled) {
                handle_webroot(nc, hm);
            }
            return;
        }
        auto req = http_request::from_hm(hm);
        if (_on_api != nullptr) {
            _on_api(conn_type_http, req.method, req.target.str(), req.body);
        }
        routing::params p;
        _http_router->route(routing::concat_method_path(req.method, req.target.path()), &p,
            [&](bool path_found, routing::params * p,  function<void(http_context *, routing::params *)> callback) {

            http_context ctx(nc, &req, hm);
            ctx.set_server(this);
            ctx.set_websocket_handshake_done(is_websocket);
            if (path_found && callback != nullptr) {
                callback(&ctx, p);
                return;
            }
            if (_webroot_enabled && req.method == "GET") {
                return handle_webroot(nc, hm);
            }
            mg_http_send_error(nc, 404, "not found");
        });
    };
    
    void handle_webroot(struct mg_connection * nc, struct http_message * p)
    {
        mg_serve_http(nc, (struct http_message *) p, *_webroot_opts);
    }

    void handle_ws_api(struct mg_connection * nc, struct websocket_message * hm)
    {
        if (!_ws_enabled) {
            return;
        }
        std::string input((const char *)hm->data, hm->size);
        json req;
        try {
            std::cout << input << std::endl;
            req = json::parse(input);
        } catch (exception ex) {
            return;
        }
        ws_conn ctx(nc, this);
        auto id_iter = req.find("id");
        if (id_iter == req.end()) {
            return;
        }
        auto id = (*id_iter).get<string>();
        auto method_iter = req.find("method");
        if (method_iter == req.end()) {
            return;
        }
        if (_on_api != nullptr) {
            _on_api(conn_type_ws, method_iter->get<std::string>(), id_iter->get<std::string>(), input);
        }
        routing::params p;
        _ws_router->route(routing::concat_method_path(method_iter->get<string>(), id_iter->get<string>()), &p,
            [&ctx, &req](bool path_found, routing::params * p, std::function<void(ws_conn *, const json &)> call) {
                if (path_found && call != nullptr) {
                    call(&ctx, req);
                    return;
                }
            });
    }

    void set_on_ws_close(std::function<void(const ws_conn &)> on_ws_close)
    {
        _on_ws_close = on_ws_close;
    }

    void set_on_http_close(std::function<void(http_server * s, mg_connection * nc)> on_http_close)
    {
        _on_http_close = on_http_close;
    }

    void handle_send_next(struct mg_connection * nc)
    {
        if (_send_next.find(nc) == _send_next.end()) {
            return;
        }
        auto sender = _send_next[nc];
        if (!sender->send_complete()) {
            sender->send(nc);
            return;
        } 
        sender->send_ok(nc);
        sender->close();
        delete sender;
        _send_next.erase(nc);
    }

    static void mongoose_http_ev_handler(struct mg_connection *nc, int ev, void *ev_data)
    {
        auto s = (http_server*)(nc->user_data);
        s->handle_send_next(nc);
        switch (ev) {
            case MG_EV_HTTP_REQUEST:
                s->handle_http_api(nc, (struct http_message *) ev_data);
                break;
            case MG_EV_WEBSOCKET_FRAME:
                s->handle_ws_api(nc, (struct websocket_message *) ev_data);
                break;
            case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
                s->handle_http_api(nc, (struct http_message *) ev_data, true);
                break;
            case MG_EV_CLOSE:
                if (is_websocket(nc)) {
                    s->on_ws_close(ws_conn{ nc, s });
                } else {
                    s->on_http_close(nc);
                }
                break;
        }
        s->send();
    };
    
    void listen(int port)
    {
        /* Open listening socket */
        mg_mgr_init(&_mgr, NULL);
        char addr[16];
        sprintf(addr, ":%d", port);
        auto nc = mg_bind(&_mgr, addr, http_server::mongoose_http_ev_handler);
        nc->user_data = this;
        mg_set_protocol_http_websocket(nc);
        _listening = true;
    }

    void poll(size_t interval = 10)
    {
        if (!_listening) {
            throw "not listening port";
        }
        _stoped = false;
        while (!_stop) {
            mg_mgr_poll(&_mgr, interval);
        }
        mg_mgr_free(&_mgr);
        _stoped = true;
    }

    ~http_server()
    {
        _stop = true;
    }
};

} }