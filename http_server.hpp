#pragma once

#include "routing.hpp"
#include "3rd/json.hpp"
#include "3rd/mongoose.h"
#include "http_message.hpp"
#include <sstream>
#include <string>
#include <functional>
#include <map>

using m_http_message = struct http_message;

namespace boo { namespace network {

using nlohmann::json;

class http_server {

public:
class http_context {
    struct mg_connection * _nc;
    http_request * _req;
public:
    http_context(struct mg_connection * nc, http_request * r): _nc(nc), _req(r)
    {
    }

    mg_connection * conn() {
        return _nc;
    }

    http_request * req() {
        return _req;
    }

    void close()
    {
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

    void send(int status_code, const std::string & body, std::map<std::string, std::string> * headers = nullptr)
    {
        if (_nc == nullptr) {
            throw "没有建立连接";
        }
        std::string header;
        bool chunk = false;
        if (headers != nullptr) {
            chunk = headers->find("Transfer-Encoding") != headers->end() && (*headers)["Transfer-Encoding"] == "chunked";
            int idx = 0;
            for (auto i = headers->begin(); i != headers->end(); ++i) {
                header += i->first + ": " + i->second;
                if (idx++ < headers->size() - 1) {
                    header += "\r\n";
                }
            }
        }
        if (!chunk) {
            mg_send_head(_nc, status_code, body.length(), header.c_str());
            mg_printf(_nc, "%s", body.c_str());
            return;
        }
        mg_send_head(_nc, status_code, -1, header.c_str());
        mg_printf_http_chunk(_nc, "%s", body.c_str());
        mg_send_http_chunk(_nc, "", 0);
    }
};

class ws_conn {
    struct mg_connection * _nc;
    bool _valid_req = true;
public:
    json req;
    ws_conn(struct mg_connection * conn): _nc(conn) {}

    bool is_valid() {
        return _valid_req;
    }

    void send(const json & data)
    {
        stringstream out;
        out << data;
        std::string msg = out.str();

        mg_send_websocket_frame(_nc, WEBSOCKET_OP_TEXT, msg.c_str(), msg.length());
    }
};

private:
    mg_mgr _mgr;
    routing::router<function<void(ws_conn *, const json &)>> * _ws_router = nullptr;
    routing::router<function<void(http_context *, routing::params *)>> * _http_router = nullptr;

    struct mg_connection * _nc = nullptr;
    std::function<void(http_context *)> _on_ws;
    std::function<void(struct mg_connection *)> _on_ws_close;

    bool _stop = false;
    bool _ws_enabled = false;
    bool _webroot_enabled = false;
    bool _http_api_enabled = false;

    struct mg_serve_http_opts * _webroot_opts;

    static int is_websocket(const struct mg_connection *nc) {
        return nc->flags & MG_F_IS_WEBSOCKET;
    };

    void on_ws(struct mg_connection * nc, m_http_message * hm) {
        if (_on_ws != nullptr) {
            auto req = http_request::from_hm(hm);
            http_server::http_context ctx{nc, &req};
            _on_ws(&ctx);
        }
    };

    void stop() {
        _stop = true;
    }

    void on_ws_close(struct mg_connection * nc) {
        if (_on_ws_close != nullptr) {
            _on_ws_close(nc);
        }
    };
    
public:
    http_server()
    {
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

    void handle_http_api(struct mg_connection * nc, struct http_message * hm)
    {
        if (!_http_api_enabled) {
            if (_webroot_enabled) {
                handle_webroot(nc, hm);
            }
            return;
        }
        auto req = http_request::from_hm(hm);
        routing::params p;
        _http_router->route(routing::concat_method_path(req.method, req.target.path()), &p,
            [&](bool path_found, routing::params * p,  function<void(http_context *, routing::params *)> callback) {

            http_context ctx(nc, &req);
            if (path_found && callback != nullptr) {
                callback(&ctx, p);
                return;
            }
            if (_webroot_enabled) {
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
            req = json::parse(input);
        } catch (exception ex) {
            return;
        }
        ws_conn ctx(nc);
        auto id_iter = req.find("id");
        if (id_iter == req.end()) {
            return;
        }
        auto id = (*id_iter).get<string>();
        auto method_iter = req.find("method");
        if (method_iter == req.end()) {
            return;
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

    void set_on_ws(std::function<void(http_context *)> on_ws) {
        _on_ws = on_ws;
    }

    void set_on_ws_close(std::function<void(struct mg_connection *)> on_ws_close)
    {
        _on_ws_close = on_ws_close;
    }


    static void mongoose_http_ev_handler(struct mg_connection *nc, int ev, void *ev_data)
    {
        auto s = (http_server*)nc->user_data;
        switch (ev) {
            case MG_EV_HTTP_REQUEST:
                s->handle_http_api(nc, (struct http_message *) ev_data);
                break;
            case MG_EV_WEBSOCKET_FRAME:
                s->handle_ws_api(nc, (struct websocket_message *) ev_data);
                break;
            case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
                s->on_ws(nc, (struct http_message *) ev_data);
                break;
            case MG_EV_CLOSE:
                if (is_websocket(nc)) {
                    s->on_ws_close(nc);
                }
                break;
        }
    };
    
    void listen(int port)
    {
        /* Open listening socket */
        mg_mgr_init(&_mgr, NULL);
        auto nc = mg_bind(&_mgr, ":8080", http_server::mongoose_http_ev_handler);
        nc->user_data = this;
        mg_set_protocol_http_websocket(nc);
    }

    void poll(size_t interval = 10)
    {
        while (!_stop) {
            mg_mgr_poll(&_mgr, interval);
        }
        mg_mgr_free(&_mgr);
    }

    ~http_server()
    {
        _stop = true;
    }
};

} }