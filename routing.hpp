#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <map>

namespace boo { namespace network {

using namespace std;

class routing {
    class param
    {
    public:
        param() {}
        param(string val) : _val(val) {}
        ~param() {}
        int to_int()
        {
            return atoi(_val.c_str());
        }
        double to_double()
        {
            return atof(_val.c_str());
        }
        bool to_bool()
        {
            if (_val == "TRUE" || _val == "true" || _val != "0") {
                return true;
            }
            return false;
        }
        string to_string()
        {
            return _val;
        }

    private:
        string _val;
    };

public:
    typedef map<string, param> params;

    enum method {
        post,
        get,
        put,
        del,
        patch,
        options,
        trace,
        head,
        connect,
        other,
    };

    static std::string method_str(method m)
    {
        std::map<method, std::string> methods{
            {post, "POST"},
            {get, "GET"},
            {options, "OPTIONS"},
            {trace, "TRACE"},
            {del, "DELETE"},
            {put, "PUT"},
            {patch, "PATCH"},
            {head, "HEAD"},
            {connect, "CONNECT"},
        };

        return methods[m];
    }

    static method method_from_str(const std::string & str)
    {
        std::map<std::string, method> methods{
            {"POST", post},
            {"GET", get},
            {"OPTIONS", options},
            {"TRACE", trace},
            {"DELETE", del},
            {"PUT", put},
            {"PATCH", patch},
            {"HEAD", head},
            {"CONNECT", connect},
        };

        return methods[str];
    }

    static std::string concat_method_path(const std::string & m, const std::string & path)
    {
        std::string tpath = path;
        if (tpath.at(tpath.length() - 1) != '/') {
            tpath.append("/");
        }
        tpath.append(m);

        return tpath;
    }

    static std::string concat_method_path(method m, const std::string & path)
    {
        return concat_method_path(method_str(m), path);
    }

private:

    enum is_a { a_unknown, a_path, a_method, a_callback, a_param };

    template<class callback_t> class node : public enable_shared_from_this<node<callback_t>>
    {
    public:
        shared_ptr<node<callback_t>> add_child(shared_ptr<node<callback_t>> n)
        {
            auto likely_n = get_likely_child(n);
            if (likely_n != nullptr) {
                return likely_n;
            }
            n->parent = this->shared_from_this();
            if (left == nullptr) {
                left = n;
                last = n;
                return n;
            }
            last->right = n;
            last = n;

            return n;
        }

        shared_ptr<node<callback_t>> get_likely_child(shared_ptr<node<callback_t>> a)
        {
            auto n = left;
            while (n != nullptr) {
                if (n->is != a->is) {
                    n = n->right;
                    continue;
                }
                switch (n->is) {
                case is_a::a_method:
                    if (n->m == a->m) {
                        return n;
                    }
                    break;
                case is_a::a_param:
                    return n;
                case is_a::a_path:
                    if (n->path == a->path) {
                        return n;
                    }
                    break;
                }
                n = n->right;
            }

            return nullptr;
        }

        vector<shared_ptr<node<callback_t>>> find_path_child(const string & path)
        {
            vector<shared_ptr<node<callback_t>>> ret;
            auto n = left;
            while (n != nullptr) {
                if (n->is == is_a::a_path && path == n->path || n->is == a_param) {
                    ret.push_back(n);
                }
                n = n->right;
            }

            return ret;
        }

        shared_ptr<node<callback_t>> find_method_child(method m)
        {
            auto n = left;
            while (n != nullptr) {
                if (n->is == a_method && m == n->m) {
                    return n;
                }
                n = n->right;
            }

            return nullptr;
        }

        shared_ptr<node<callback_t>> find_callback_child()
        {
            auto n = left;
            while (n != nullptr) {
                if (n->is == a_callback) {
                    return n;
                }
                n = n->right;
            }

            return nullptr;
        };
    public:
        is_a is = a_unknown;

        callback_t callback;
        string path;
        method m;

        shared_ptr<node<callback_t>> parent = nullptr;
        shared_ptr<node<callback_t>> left = nullptr;
        shared_ptr<node<callback_t>> right = nullptr;
        shared_ptr<node<callback_t>> last = nullptr;
    };

public:
    template<class callback_t> class router
    {
    public:
        void on(const string & p, callback_t on)
        {
            std::string path(p);
            shared_ptr<node<callback_t>> n = _head;
            auto pos = path.find('/');
            string s = "";
            auto handle_path = [](const string & s, shared_ptr<node<callback_t>> n) -> shared_ptr<node<callback_t>> {
                auto c = make_shared<node<callback_t>>();
                if (s[0] == '{' && s[s.length() - 1] == '}') {
                    c->is = is_a::a_param;
                    c->path = s.substr(1, s.length() - 2);
                }
                else {
                    c->is = is_a::a_path;
                    c->path = s;
                }
                return n->add_child(c);
            };
            while (pos != string::npos) {
                s = path.substr(0, pos);
                path = path.substr(pos + 1);
                pos = path.find('/');
                if (s.length() == 0) {
                    continue;
                }
                n = handle_path(s, n);
            }
            n = handle_path(path, n);
            auto c = std::make_shared<node<callback_t>>();
            c->is = is_a::a_callback;
            c->callback = on;
            n->add_child(c);
        }

        void on(method m, const string & path, callback_t on)
        {
            this->on(routing::concat_method_path(m, path), on);
        }

        void on(int id, callback_t on)
        {
            this->on(std::to_string(id), on);
        }

        void un(routing::method m, const std::string & path)
        {
            params p;
            vector<shared_ptr<node<callback_t>>> ns = match_path(path, &p);
            if (ns.size() == 0) {
                return;
            }
            for (auto i = ns.begin(); i != ns.end(); ++i) {
                auto n = (*i)->find_method_child(m);
                if (n != nullptr) {
                    n->find_callback_child()->callback = nullptr;
                    return;
                }
            }
        }
    
        typedef function<void(bool, params *, callback_t)> r_callback_t;
    
        shared_ptr<node<callback_t>> head()
        {
            return _head->shared_from_this();
        }
    
        void route(int id, r_callback_t r_callback)
        {
            params p;
            route(std::to_string(id), &p, r_callback);
        }

        void route(method m, string path, params * p, r_callback_t r_callback)
        {
            route(concat_method_path(m, path), p, r_callback);
        }

        void route(string path, params * p, r_callback_t r_callback)
        {
            vector<shared_ptr<node<callback_t>>> ns = match_path(path, p);
            if (ns.size() == 0) {
                r_callback(false, p, nullptr);
                return;
            }
            auto c = ns[0]->find_callback_child();
            if (c == nullptr) {
                r_callback(false, p, nullptr);
                return;
            }
            r_callback(true, p, c->callback);
        }
    
    private:
        vector<shared_ptr<node<callback_t>>> match_path(string req_path, params * p, shared_ptr<node<callback_t>> n = nullptr)
        {
            if (n == nullptr) {
                n = head();
            }
            auto pos = req_path.find('/');
            vector<shared_ptr<node<callback_t>>> ret = { n };
            string s;
            while (pos != string::npos) {
                s = req_path.substr(0, pos);
                req_path = req_path.substr(pos + 1);
                pos = req_path.find('/');
                if (s.length() == 0) {
                    continue;
                }
                auto children = n->find_path_child(s);
                for (auto i = children.begin(); i != children.end(); ++i) {
                    auto n = *i;
                    ret = match_path(req_path, p, n);
                    if (ret.size() == 0) {
                        continue;
                    }
                    if (n->is == is_a::a_param) {
                        (*p)[n->path] = param(s);
                    }
                    return ret;
                }
            }
            if (ret.size() == 0 || req_path == "") {
                return ret;
            }
            vector<shared_ptr<node<callback_t>>> ends;
            for (auto e = ret.begin(); e != ret.end(); ++e) {
                auto en = *e;
                auto children = en->find_path_child(req_path);
                if (children.size() == 0) {
                    continue;
                }
                for (auto i = children.begin(); i != children.end(); ++i) {
                    ends.push_back(*i);
                    if ((*i)->is == is_a::a_param) {
                        (*p)[(*i)->path] = param(req_path);
                    }
                }
            }
            return ends;
        }
    
        //
        // 假设有
        //
        // POST         /hello/world
        // GET          /hello/world
        // GET          /hello
        // DELETE       /hello
        // 
        // 这棵树会是这个样子
        //
        //                              HEAD
        //                               |
        //                             hello
        //                             / | \
        //                            /  |  \
        //                           /   |   \
        //                      world   GET  DELETE
        //                       / \     \        \
        //                      /   \     \        \
        //                   POST   GET CALLBACK  CALLBACK
        //                    /       \
        //                CALLBACK  CALLBACK
        // 
        //
        shared_ptr<node<callback_t>> _head = std::make_shared<node<callback_t>>();
    };

};

} } // vod::network::routing

