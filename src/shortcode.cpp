#include "shortcode.h"

#include <cstdlib>
#include <string>

#include "config.h"
#include "json.h"
#include "lobby.h"
#include "util.h"

static std::string base_url() {
    std::string u = trim_str(short_url);
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

bool short_valid_code(const std::string &code) {
    if (code.size() < 3 || code.size() > 6) return false;
    for (char c : code)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool short_publish(const std::string &nwid, std::string &code, std::string &err,
                   long long *expires) {
    std::string base = base_url();
    if (base.empty()) { err = "short-code service not configured"; return false; }
    std::string resp;
    std::string body = "{\"nwid\":\"" + nwid + "\"}";
    if (!http_post_json(base + "/publish", body, resp, err)) return false;
    if (!json_object_field(resp, "code", code) || !short_valid_code(code)) {
        err = "bad publish reply";
        return false;
    }
    if (expires) {
        std::string exp;
        *expires = json_object_field(resp, "expires", exp) ? atoll(exp.c_str()) : 0;
    }
    return true;
}

bool short_resolve(const std::string &code, std::string &nwid, std::string &err) {
    std::string base = base_url();
    if (base.empty()) { err = "short-code service not configured"; return false; }
    if (!short_valid_code(code)) { err = "bad code"; return false; }
    std::string resp;
    if (!http_get(base + "/resolve?code=" + code, resp, err)) return false;
    if (!json_object_field(resp, "nwid", nwid) || nwid.size() != 16) {
        err = "short code not found";
        return false;
    }
    return true;
}
