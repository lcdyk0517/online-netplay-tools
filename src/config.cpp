#include "config.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"

const char *RA_CFG64 = "/home/ark/.config/retroarch/retroarch.cfg";
const char *RA_CFG32 = "/home/ark/.config/retroarch32/retroarch.cfg";


std::string nick = "darkos3562";
bool use_mitm = false;
std::string mitm_server = "nyc";
std::string custom_mitm;
bool announce = true;
std::string proxy;
std::string zt_net;
std::string zt_own;
bool zt_host = false;

static std::string esc_cfg(const std::string &v) {
    std::string o;
    o.reserve(v.size() + 8);
    for (char c : v) {
        if (c == '\r' || c == '\n') { o += ' '; continue; }
        if (c == '\\' || c == '"') o += '\\';
        o += c;
    }
    return o;
}

static std::string unesc_cfg(const std::string &v) {
    std::string o;
    o.reserve(v.size());
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size()) { o += v[++i]; continue; }
        o += v[i];
    }
    return o;
}

bool cfg_get(const char *path, const std::string &key, std::string &out) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim_str(line);
        if (t.rfind(key, 0) != 0) continue;
        size_t p = key.size();
        while (p < t.size() && (t[p] == ' ' || t[p] == '\t')) p++;
        if (p >= t.size() || t[p] != '=') continue;
        out = trim_str(t.substr(p + 1));
        if (out.size() >= 2 && out.front() == '"' && out.back() == '"')
            out = unesc_cfg(out.substr(1, out.size() - 2));
        return true;
    }
    return false;
}

void cfg_set(const char *path, const std::string &key, const std::string &value) {
    std::ifstream in(path);
    if (!in) return;
    std::vector<std::string> lines;
    std::string line;
    bool done = false;
    while (std::getline(in, line)) {
        std::string t = trim_str(line);
        if (!done && t.rfind(key, 0) == 0) {
            size_t p = key.size();
            while (p < t.size() && (t[p] == ' ' || t[p] == '\t')) p++;
            if (p < t.size() && t[p] == '=') { line = key + " = \"" + esc_cfg(value) + "\""; done = true; }
        }
        lines.push_back(line);
    }
    if (!done) lines.push_back(key + " = \"" + esc_cfg(value) + "\"");
    std::string tmp = std::string(path) + ".ra-lobby.tmp";
    std::ofstream out(tmp);
    if (!out) return;
    for (auto &l : lines) out << l << "\n";
    out.close();
    // Keep the original owner: RetroArch runs as ark and rewrites this file.
    struct stat st;
    if (stat(path, &st) == 0) {
        chmod(tmp.c_str(), st.st_mode & 07777);
        if (chown(tmp.c_str(), st.st_uid, st.st_gid) != 0) { /* best effort */ }
    }
    rename(tmp.c_str(), path);
}

void load_cfg() {
        std::ifstream in("/home/ark/.config/ra-lobby.cfg");
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("NICK=", 0) == 0 && line.size() > 5) nick = line.substr(5);
            if (line.rfind("PROXY=", 0) == 0) proxy = line.substr(6);
            if (line.rfind("ZTNET=", 0) == 0) zt_net = lower(line.substr(6));
            if (line.rfind("ZTOWN=", 0) == 0) zt_own = lower(line.substr(6));
            if (line.rfind("ZTHOST=", 0) == 0) zt_host = (line.substr(7) == "1");
        }
        if (zt_host && zt_own.empty()) zt_own = zt_net;
}

bool save_cfg() {
    auto clean = [](const std::string &v) {
        std::string o;
        for (char c : v) o += (c == '\n' || c == '\r') ? ' ' : c;
        return trim_str(o);
    };
    std::string n = clean(nick);
    std::string px = clean(proxy);
    std::string zn = clean(zt_net);
    std::string zo = clean(zt_own);
    mkdir("/home/ark/.config", 0755);
    std::string tmp = "/home/ark/.config/ra-lobby.cfg.tmp";
    std::ofstream out(tmp);
    if (!out) return false;
    out << "NICK=" << n << "\n";
    if (!px.empty()) out << "PROXY=" << px << "\n";
    if (!zn.empty()) out << "ZTNET=" << zn << "\n";
    if (!zo.empty()) out << "ZTOWN=" << zo << "\n";
    out << "ZTHOST=" << (zt_host ? "1" : "0") << "\n";
    out.flush();
    if (!out) return false;
    out.close();
    return rename(tmp.c_str(), "/home/ark/.config/ra-lobby.cfg") == 0;
}

void load_ra_all() {
    auto get = [&](const char *k, std::string &v) {
        std::string t;
        if (cfg_get(RA_CFG64, k, t) && !t.empty()) { v = t; return true; }
        if (cfg_get(RA_CFG32, k, t) && !t.empty()) { v = t; return true; }
        return false;
    };
    std::string v;
    if (get("netplay_nickname", v)) nick = v;
    if (get("netplay_use_mitm_server", v)) use_mitm = (v == "true" || v == "1");
    if (get("netplay_mitm_server", v) && !v.empty()) mitm_server = v;
    if (get("netplay_custom_mitm_server", v)) custom_mitm = v;
    if (get("netplay_public_announce", v)) announce = (v == "true" || v == "1");
}

void save_ra_all() {
    for (const char *path : { RA_CFG64, RA_CFG32 }) {
        cfg_set(path, "netplay_nickname", nick);
        cfg_set(path, "netplay_use_mitm_server", use_mitm ? "true" : "false");
        cfg_set(path, "netplay_mitm_server", mitm_server);
        if (!custom_mitm.empty()) cfg_set(path, "netplay_custom_mitm_server", custom_mitm);
        cfg_set(path, "netplay_public_announce", announce ? "true" : "false");
    }
}
