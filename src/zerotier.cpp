#include "zerotier.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/wait.h>
#include <curl/curl.h>

#include "json.h"
#include "util.h"

static std::string run_capture(const std::string &cmd, int *exit_status = nullptr) {
    std::string out;
    if (exit_status) *exit_status = -1;
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) out += buf;
    int rc = pclose(p);
    if (exit_status) {
        if (rc == -1) *exit_status = -1;
        else if (WIFEXITED(rc)) *exit_status = WEXITSTATUS(rc);
        else if (WIFSIGNALED(rc)) *exit_status = 128 + WTERMSIG(rc);
        else *exit_status = rc;
    }
    return trim_str(out);
}

const char *ZT_BIN     = "/opt/online-tool/zerotier-one";
const char *ZT_SYS     = "/usr/sbin/zerotier-one";
const char *ZT_PORT    = "/var/lib/zerotier-one/zerotier-one.port";
const char *ZT_TUN     = "/dev/net/tun";

bool zt_installed(void) {
    const char *e = getenv("RA_ZT_BIN");
    if (e && *e) return access(e, X_OK) == 0;
    return access(ZT_BIN, X_OK) == 0 || access(ZT_SYS, X_OK) == 0;
}
static std::string zt_bin(void) {
    const char *e = getenv("RA_ZT_BIN");
    if (e && *e) return std::string(e);
    return access(ZT_BIN, X_OK) == 0 ? std::string(ZT_BIN) : std::string(ZT_SYS);
}
bool zt_tun_ok(void)     { return access(ZT_TUN, F_OK) == 0; }

bool zt_daemon_up(void)  { return access(ZT_PORT, R_OK) == 0; }

bool zt_api_ready(void) {
    if (!zt_daemon_up()) return false;
    int rc = 0;
    std::string out = run_capture("timeout 4 sudo -n " + shq(zt_bin()) + " -q info 2>&1", &rc);
    return rc == 0 && out.rfind("200 info", 0) == 0;
}

/* ArkOS4Clone's sudo secure_path has no /sbin, so "modprobe" is not found
 * there; call it by absolute path. 3326 ships tun as a module. */
bool zt_tun_ensure(void) {
    if (zt_tun_ok()) return true;
    int rc = 0;
    run_capture("timeout 5 sudo -n /sbin/modprobe tun 2>&1", &rc);
    if (!zt_tun_ok()) run_capture("timeout 5 sudo -n /usr/sbin/modprobe tun 2>&1", &rc);
    return zt_tun_ok();
}

bool zt_ensure_daemon(void) {
    if (zt_api_ready()) return true;
    if (!zt_installed()) return false;
    /* Start with system(): the daemon detaches (-d) and inherits its stdio,
     * so a popen() around it would block on the pipe forever. -U keeps root
     * (this image has no zerotier system user). Do not trust the port file:
     * it survives a killed daemon, and skipping the start then leaves a dead
     * service behind. An extra start when one is already running exits fast. */
    {
        std::string cmd = "timeout 8 sudo -n " + shq(zt_bin())
                        + " -U -d >/tmp/ra-lobby-zt.log 2>&1";
        int rc = std::system(cmd.c_str());
        int code = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc)
                 : (rc != -1 && WIFSIGNALED(rc)) ? 128 + WTERMSIG(rc) : rc;
        if (code != 0)
            fprintf(stderr, "zerotier-one start exited %d\n", code);
    }
    for (int i = 0; i < 32; i++) {
        if (zt_api_ready()) return true;
        usleep(250000);
    }
    return false;
}

std::string zt_cli(const std::string &verb, const std::string &id) {
    if (!zt_ensure_daemon()) return "";
    int rc = 0;
    std::string out = run_capture("timeout 6 sudo -n " + shq(zt_bin()) + " -q " +
                                  shq(verb) + " " + shq(id) + " 2>&1", &rc);
    if (out.rfind("200", 0) != 0)
        fprintf(stderr, "zerotier %s %s failed (rc=%d): %s\n",
                verb.c_str(), id.c_str(), rc, out.c_str());
    return out;
}

bool zt_valid_id(const std::string &id) {
    if (id.size() != 16) return false;
    for (char c : id) if (!isxdigit((unsigned char)c)) return false;
    return true;
}

void zt_parse_info(const std::string &info, ZtStatus &st) {
    std::stringstream ss(info);
    std::string t;
    std::vector<std::string> tok;
    while (ss >> t) tok.push_back(t);
    if (tok.size() >= 3 && tok[0] == "200") st.node = tok[2];
}

void zt_parse_networks(const std::string &nets, const std::string &netid, ZtStatus &st) {
    st.found = false;
    st.ok    = false;
    st.ip.clear();
    std::stringstream ss(nets);
    std::string line;
    while (std::getline(ss, line)) {
        std::stringstream ls(line);
        std::string t;
        std::vector<std::string> tok;
        while (ls >> t) tok.push_back(t);
        if (tok.size() < 8 || tok[0] != "200" || tok[1] != "listnetworks") continue;
        if (!netid.empty() && lower(tok[2]) != lower(netid)) continue;
        st.found = true;
        /* The network name may contain spaces, so do not index by column:
         * take the status token by keyword and the IP from the tail. */
        for (size_t k = 3; k < tok.size(); k++) {
            const std::string &t = tok[k];
            if (t == "OK") { st.ok = true; st.state = "ok"; break; }
            if (t == "ACCESS_DENIED") { st.state = "denied"; break; }
            if (t == "REQUESTING_CONFIGURATION") { st.state = "requesting"; break; }
            if (t == "NOT_FOUND") { st.state = "notfound"; break; }
        }
        for (size_t k = tok.size(); k-- > 3;) {
            std::string a = tok[k];
            size_t slash = a.find('/');
            if (slash != std::string::npos) a = a.substr(0, slash);
            if (a.find('.') != std::string::npos && a.find(':') == std::string::npos) { st.ip = a; break; }
        }
        break;
    }
    if (!st.found) st.state = "notjo";
}

ZtStatus zt_status(const std::string &netid) {
    ZtStatus st;
    if (!zt_installed()) { st.state = "noinst"; return st; }
    if (!zt_tun_ok())    { st.state = "notun"; return st; }
    if (!zt_daemon_up()) { st.state = "down"; return st; }
    zt_parse_info(run_capture("timeout 6 sudo -n " + shq(zt_bin()) + " -q info 2>&1"), st);
    zt_parse_networks(run_capture("timeout 6 sudo -n " + shq(zt_bin()) + " -q listnetworks 2>&1"), netid, st);
    return st;
}


// -------- local controller API (self-hosted network, no my.zerotier.com) --------
std::string zt_home(void) {
    const char *e = getenv("RA_ZT_HOME");
    return (e && *e) ? std::string(e) : std::string("/var/lib/zerotier-one");
}

std::string zt_read_file(const std::string &path) {
    std::ifstream in(path);
    if (!in) return "";
    std::stringstream ss;
    ss << in.rdbuf();
    return trim_str(ss.str());
}

std::string zt_token(void) {
    static std::string cached;
    if (!cached.empty()) return cached;
    std::string p = zt_home() + "/authtoken.secret";
    cached = zt_read_file(p);
    if (cached.empty()) cached = run_capture("timeout 4 sudo -n cat " + shq(p) + " 2>/dev/null");
    return cached;
}

std::string zt_port(void) {
    std::string p = trim_str(zt_read_file(zt_home() + "/zerotier-one.port"));
    if (p.empty()) return "9993";
    for (char c : p) if (!isdigit((unsigned char)c)) return "9993";
    return p;
}

bool zt_api(const std::string &method, const std::string &path,
                   const std::string &body, std::string &out, std::string &err) {
    std::string token = zt_token();
    if (token.empty()) { err = "no authtoken"; return false; }
    std::string url = "http://127.0.0.1:" + zt_port() + path;
    CURL *c = curl_easy_init();
    if (!c) { err = "curl init"; return false; }
    struct curl_slist *hdr = nullptr;
    hdr = curl_slist_append(hdr, ("X-ZT1-Auth: " + token).c_str());
    if (!body.empty()) hdr = curl_slist_append(hdr, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    if (method == "POST") {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    } else if (method != "GET") {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!body.empty()) curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    }
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) { err = curl_easy_strerror(rc); return false; }
    if (code >= 300) { err = "HTTP " + std::to_string(code) + ": " + out.substr(0, 120); return false; }
    return true;
}


std::vector<ZtMember> zt_fetch_members(const std::string &nwid) {
    std::vector<ZtMember> out;
    std::string resp, err;
    if (!zt_api("GET", "/controller/network/" + nwid + "/member", "", resp, err)) return out;
    JsonReader ids(resp);
    if (!ids.eat('[')) return out;
    std::vector<std::string> list;
    while (true) {
        ids.ws();
        if (ids.i >= ids.s.size()) break;
        if (ids.s[ids.i] == ']') break;
        std::string id;
        if (!ids.str(id)) break;
        if (id.size() == 10) list.push_back(id);
        ids.ws();
        if (ids.i < ids.s.size() && ids.s[ids.i] == ',') { ids.i++; continue; }
        break;
    }
    for (auto &id : list) {
        ZtMember m;
        m.id = id;
        std::string obj, e2;
        if (zt_api("GET", "/controller/network/" + nwid + "/member/" + id, "", obj, e2)) {
            std::string v;
            if (json_object_field(obj, "authorized", v)) m.authorized = (v == "true");
            if (json_string_array_first(obj, "ipAssignments", v) &&
                v.find('.') != std::string::npos)
                m.ip = v;
        }
        out.push_back(m);
    }
    return out;
}

bool zt_delete_member(const std::string &nwid, const std::string &id) {
    std::string resp, err;
    return zt_api("DELETE", "/controller/network/" + nwid + "/member/" + id, "", resp, err);
}

std::vector<std::string> zt_controller_networks(void) {
    std::vector<std::string> out;
    std::string resp, err;
    if (!zt_api("GET", "/controller/network", "", resp, err)) return out;
    JsonReader r(resp);
    if (!r.eat('[')) return out;
    while (true) {
        r.ws();
        if (r.i >= r.s.size()) break;
        if (r.s[r.i] == ']') break;
        std::string id;
        if (!r.str(id)) break;
        if (id.size() == 16) out.push_back(id);
        r.ws();
        if (r.i < r.s.size() && r.s[r.i] == ',') { r.i++; continue; }
        break;
    }
    return out;
}

bool zt_authorize_member(const std::string &nwid, const std::string &id, bool auth) {
    std::string resp, err;
    std::string body = std::string("{\"authorized\":") + (auth ? "true" : "false") + "}";
    return zt_api("POST", "/controller/network/" + nwid + "/member/" + id, body, resp, err);
}

/* Creates a private 10.147.17.0/24 network on the local controller and
 * returns its 16-hex id (controller address + 8 hex). */
std::string zt_create_network(const std::string &controller_id) {
    if (controller_id.size() != 10) return "";
    /* Pick a /24 that no network of this controller uses yet. The starting
     * point is derived from the controller address, but sequential creates
     * must not land on the same subnet (two zt interfaces with one IP break
     * routing). */
    unsigned x = (unsigned)strtoul(controller_id.substr(0, 2).c_str(), nullptr, 16);
    unsigned y = (unsigned)strtoul(controller_id.substr(2, 2).c_str(), nullptr, 16);
    std::vector<std::string> used;
    for (auto &nw : zt_controller_networks()) {
        std::string obj, err;
        if (!zt_api("GET", "/controller/network/" + nw, "", obj, err)) continue;
        std::vector<std::string> routes;
        json_object_array_items(obj, "routes", routes);
        for (auto &route : routes) {
            std::string t;
            if (json_object_field(route, "target", t) && t.rfind("10.", 0) == 0)
                used.push_back(t);
        }
    }
    std::string base;
    unsigned start = y % 254;
    for (unsigned i = 0; i < 254 && base.empty(); i++) {
        std::string cand = "10." + std::to_string(128 + (x % 96)) + "." +
                           std::to_string(1 + ((start + i) % 254));
        bool taken = false;
        for (auto &t : used)
            if (t.rfind(cand + ".", 0) == 0) { taken = true; break; }
        if (!taken) base = cand;
    }
    if (base.empty()) return "";
    std::string body =
        "{\"name\":\"dArkOS Netplay\",\"private\":true,\"enableBroadcast\":true,"
        "\"v4AssignMode\":{\"zt\":true},"
        "\"ipAssignmentPools\":[{\"ipRangeStart\":\"" + base + ".1\",\"ipRangeEnd\":\"" + base + ".254\"}],"
        "\"routes\":[{\"target\":\"" + base + ".0/24\"}]}";
    std::string resp, err;
    if (!zt_api("POST", "/controller/network/" + controller_id + "______", body, resp, err))
        return "";
    std::string nwid;
    if (!json_object_field(resp, "nwid", nwid) || nwid.empty())
        json_object_field(resp, "id", nwid);
    return nwid;
}
