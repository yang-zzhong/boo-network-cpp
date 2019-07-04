#pragma once

#include "3rd/mongoose.h"
#include "target.hpp"
#include <string>
#include <map>

using m_http_message = struct http_message;

namespace boo {namespace network {

class http_request {
public:
    std::string body;
    std::map<std::string, std::string> headers;
    target_t target;
    std::string method;

    http_request(const std::string & u, const std::string & m): target(u), method(m) {}
    http_request() {}

    static http_request from_hm(m_http_message * hm)
    {
        std::string target(hm->uri.p, hm->uri.len);
        if (hm->query_string.len > 0) {
            target += "?" + std::string(hm->query_string.p, hm->query_string.len);
        }
        std::string method(hm->method.p, hm->method.len);
        http_request req(target, method);
        req.body = std::string(hm->body.p, hm->body.len);
        for (int i = 0; hm->header_names[i].len > 0; ++i) {
            std::string header_name(hm->header_names[i].p, hm->header_names[i].len);
            std::string header_val(hm->header_values[i].p, hm->header_values[i].len);
            req.headers[header_name] = header_val;
        }
        return req;
    }
};

class http_response {
public:
    int status_code;
    std::string body;
    std::map<std::string, std::string> headers;

    http_response(int status_code) : status_code(status_code) {}
    http_response() {}

    static http_response from_hm(m_http_message * hm)
    {
        http_response res(hm->resp_code);
        res.body = std::string(hm->body.p, hm->body.len);
        for (int i = 0; hm->header_names[i].len > 0; ++i) {
            std::string header_name(hm->header_names[i].p, hm->header_names[i].len);
            std::string header_val(hm->header_values[i].p, hm->header_values[i].len);
            res.headers[header_name] = header_val;
        }

        return res;
    }
};

}} 