#pragma once

#include <string>
#include <map>

class target_t {
public:
    target_t() { }

    target_t(const std::string & str)
    {
        std::string input(str);
        auto qp = input.find('?');
        if (qp == std::string::npos) {
            path(input);
            return;
        }
        path(input.substr(0, qp));
        input = input.substr(qp + 1);
        auto sp = input.find('#');
        if (sp == std::string::npos) {
            queries(parse_query(input));
            return;
        }
        queries(parse_query(input.substr(0, sp)));
        hash(input.substr(sp + 1));
    }

    static std::map<std::string, std::string> parse_query(const std::string & query)
    {
        std::string input = query;
        std::map<std::string, std::string> ret;
        size_t pos;
        while((pos = input.find('&')) != std::string::npos) {
            std::string slice = input;
            if (pos != std::string::npos) {
                slice = input.substr(0, pos);               
                input = input.substr(pos + 1, input.length() - pos - 1);
            }
            handle_slice(ret, &slice);
        }
        handle_slice(ret, &input);

        return ret;
    }

    static void handle_slice(std::map<std::string, std::string> & ret, std::string * slice)
    {
        size_t pos = slice->find('=');
        if (pos == std::string::npos) {
            if (*slice != "") {
                ret[*slice] = "";
            }
            return;
        }
        std::string key = slice->substr(0, pos);
        std::string value = slice->substr(pos + 1, slice->length() - pos - 1);
        ret[key] = value;
    }

    const std::string & path() const
    {
        return _path;
    }

    void path(const std::string & p)
    {
        _path = p;
    }

    void push_query(const std::string & key, const std::string & val)
    {
        _queries[key] = val;
    }

    void hash(const std::string & hash)
    {
        _hash = hash;
    }

    const std::string & hash() const
    {
        return _hash;
    }

    const std::map<std::string, std::string> & queries() const
    {
        return _queries;
    }

    void queries(const std::map<std::string, std::string> & queries)
    {
        _queries = queries;
    }

    static char from_hex(char ch) {
        return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
    }

    static char to_hex(char code) {
        static char hex[] = "0123456789abcdef";
        return hex[code & 15];
    }

    static std::string url_encode(const std::string & input)
    {
        std::string out;
        for (size_t i = 0; i < input.length(); ++i) {
          if (isalnum(input[i]) || input[i] == '-' || input[i] == '_' || input[i] == '.' || input[i] == '~') {
            out += input[i];
          } else if (input[i] == ' ') {
            out += '+';
          } else {
            out += '%' + to_hex(input[i] >> 4) + to_hex(input[i] & 15);
          }
        }
        return out;
    }

    static std::string url_decode(const std::string & input)
    {
        std::string out;
        for (size_t i = 0; i < input.length(); ++i) {
            if (i < input.length() - 2 && input[i] == '%') {
                out += from_hex(input[i + 1]) << 4 | from_hex(input[i + 2]);
                i += 2;
                continue;
            }
            if (input[i] == '+') {
                out += ' ';
                continue;
            }
            out += input[i];
        }
        return out;
    }

    static std::string encode_query(const std::map<std::string, std::string> & params)
    {
        std::string s;
        size_t idx = 0;
        auto qs = params.size();
        for (auto i = params.begin(); i != params.end(); ++i) {
            if (i->first == "") {
                continue;
            }
            s.append(i->first);
            s.append("=");
            s.append(i->second);
            if (idx < qs - 1) {
                s.append("&");
            }
            idx++;
        }
        if (s[s.length() - 1] == '&') {
            s = s.substr(0, s.length() - 1);
        }
        return s;
    }

    std::string str() const
    {
        std::string s(_path);
        auto qs = _queries.size();
        if (qs > 0) {
            s.append("?");
            s.append(encode_query(_queries));
        }
        if (_hash.length() > 0) {
            s.append(_hash);
        }

        return s;
    }

    const std::string & find(const std::string & key) const
    {
        auto i = _queries.find(key);
        if (i == _queries.end()) {
            return _empty;
        }

        return _queries.at(key);
    }

private:
    std::string _path = "/";
    std::map<std::string, std::string> _queries;
    std::string _hash = "";

    std::string _empty = "";
};
