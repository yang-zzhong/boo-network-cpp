#pragma once

#include "target.hpp"
#include <string>
#include <map>
#include <list>
#include <sstream>
#include <cctype>
#include <iomanip>
#include "target.hpp"

using namespace std;

namespace boo { namespace network {

class url_t 
{
public:   
    url_t() {};
    url_t(const string & input)
    {
        parse(input);
    };

    string str() const
    {
        string url = schema + "://";
        if (username.length() > 0 && password.length() > 0) {
            url += username + ":" + password + "@";
        } else if (username.length() > 0) {
            url += username + "@";
        }
        url.append(host);
        if (port.length() != 0) {
            url.append(":" + port);
        }
        url.append(target_str());

        return url;
    };

    string target_str() const
    {
        std::string target(path == "" ? "/" : path);
        if (query.size() > 0) {
            target.append("?" + target::encode_query(query));
        }
        if (hash.length() > 0) {
            target.append("#" + hash);
        }

        return  target;
    }

    void parse(const string & input)
    {
        string url(input);
        auto pos = url.find("://");
        if (pos != string::npos) {
            schema = url.substr(0, pos);
            url = url.substr(pos + 3, url.length() - pos - 3);
        }
        list<string> seps = { "/", "?", "#" };
        parse_user_info(url);
        parse_host_and_port(parse_url(url, seps));
        if (url.length() == 0) {
            return;
        }
        seps.pop_front();
        path = parse_url(url, seps);
        if (url.length() == 0) {
            return;
        }
        seps.pop_front();
        string q = parse_url(url, seps);
        if (q.length() > 1) {
            query = target::parse_query(q.substr(1, q.length() - 1));
        }
        if (url.length() == 0) {
            return;
        }
        seps.pop_front();
        string h = parse_url(url, seps);
        if (h.length() > 1) {
            hash = h.substr(1, h.length() - 1);
        }
    };

    static string escape(const string & url)
    {
        ostringstream escaped;
        escaped.fill('0');
        escaped << hex;

        for (string::const_iterator i = url.begin(), n = url.end(); i != n; ++i) {
            string::value_type c = (*i);
            // Keep alphanumeric and other accepted characters intact
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
                continue;
            }
            // Any other characters are percent-encoded
            escaped << uppercase;
            escaped << '%' << setw(2) << int((unsigned char) c);
            escaped << nouppercase;
        }

        return escaped.str();
    };

    static string unescape(const string & url)
    {
        string ret;
        char ch;
        size_t i, ii;
        for (i = 0; i < url.length(); i++) {
            if (int(url[i]) == 37) {
                ret+=url[i];
                continue;
            }
            sscanf(url.substr(i+1,2).c_str(), "%x", &ii);
            ch = static_cast<char>( ii );
            ret += ch;
            i = i+2;
        }
        return ret;
    };

public:
    string schema = "https";
    string username = "";
    string password = "";
    string host;
    string port;
    string path;
    map<string, string> query;
    string hash;
private:

    void parse_host_and_port(const string & input)
    {
        size_t pos = input.find(":");
        if (pos == string::npos) {
            host = input;
            return;
        }
        host = input.substr(0, pos);
        port = input.substr(pos + 1, input.length() - pos - 1);
    }

    string parse_url(string & url, list<string> seps)
    {
        for (auto i = seps.begin(); i != seps.end(); ++i) {
            auto found = url.find(*i);
            if (found == string::npos) {
                continue;
            }
            string ret = url.substr(0, found);
            auto pos = found;
            url = url.substr(pos, url.length() - pos);
            return ret;
        }
        string ret = url.substr(0, url.length());
        url = "";

        return ret;
    }

    void parse_user_info(string & url)
    {
        size_t pos = url.find("@");
        if (pos == string::npos) {
            return;
        }
        string user = url.substr(0, pos);
        url = url.substr(pos + 1, user.length() - pos - 1);
        pos = user.find(":");
        if (pos == string::npos) {
            username = user;
            return;
        }
        username = user.substr(0, pos);
        password = user.substr(pos + 1, user.length() - pos - 1);
    }
};

}} 