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
#include <curl/curl.h>

#include "util.h"

static std::string run_capture(const std::string &cmd) {
    std::string out;
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
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
    std::string out = run_capture("timeout 4 sudo -n " + zt_bin() + " -q info 2>&1");
    return out.rfind("200 info", 0) == 0;
}

/* ArkOS4Clone's sudo secure_path has no /sbin, so "modprobe" is not found
 * there; call it by absolute path. 3326 ships tun as a module. */
bool zt_tun_ensure(void) {
    if (zt_tun_ok()) return true;
    run_capture("timeout 5 sudo -n /sbin/modprobe tun 2>&1");
    if (!zt_tun_ok()) run_capture("timeout 5 sudo -n /usr/sbin/modprobe tun 2>&1");
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
        std::string cmd = "timeout 8 sudo -n " + zt_bin()
                        + " -U -d >/tmp/ra-lobby-zt.log 2>&1";
        int rc = std::system(cmd.c_str());
        (void)rc;
    }
    for (int i = 0; i < 32; i++) {
        if (zt_api_ready()) return true;
        usleep(250000);
    }
    return false;
}

std::string zt_cli(const std::string &args) {
    if (!zt_ensure_daemon()) return "";
    return run_capture("timeout 6 sudo -n " + zt_bin() + " -q " + args + " 2>&1");
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
        const std::string &s = tok[5];
        if (s == "OK") { st.ok = true; st.state = "ok"; }
        else if (s == "ACCESS_DENIED") st.state = "denied";
        else if (s == "REQUESTING_CONFIGURATION") st.state = "requesting";
        else if (s == "NOT_FOUND") st.state = "notfound";
        else st.state = s;
        for (size_t k = 7; k < tok.size(); k++) {
            std::string a = tok[k];
            size_t slash = a.find('/');
            if (slash != std::string::npos) a = a.substr(0, slash);
            if (a.find('.') != std::string::npos) { st.ip = a; break; }
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
    zt_parse_info(run_capture("timeout 6 sudo -n " + zt_bin() + " -q info 2>&1"), st);
    zt_parse_networks(run_capture("timeout 6 sudo -n " + zt_bin() + " -q listnetworks 2>&1"), netid, st);
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
    if (cached.empty()) cached = run_capture("timeout 4 sudo -n cat " + p + " 2>/dev/null");
    return cached;
}

std::string zt_port(void) {
    std::string p = zt_read_file(zt_home() + "/zerotier-one.port");
    return p.empty() ? std::string("9993") : p;
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

std::string json_string_field(const std::string &j, const char *key) {
    std::string k = std::string("\"") + key + "\":\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return "";
    p += k.size();
    size_t e = j.find('"', p);
    return e == std::string::npos ? "" : j.substr(p, e - p);
}


std::vector<ZtMember> zt_fetch_members(const std::string &nwid) {
    std::vector<ZtMember> out;
    std::string resp, err;
    if (!zt_api("GET", "/controller/network/" + nwid + "/member", "", resp, err)) return out;
    /* ["id1","id2",...] */
    size_t p = 0;
    while ((p = resp.find('"', p)) != std::string::npos) {
        size_t e = resp.find('"', p + 1);
        if (e == std::string::npos) break;
        std::string id = resp.substr(p + 1, e - p - 1);
        p = e + 1;
        if (id.size() != 10) continue;
        ZtMember m;
        m.id = id;
        std::string obj, e2;
        if (zt_api("GET", "/controller/network/" + nwid + "/member/" + id, "", obj, e2)) {
            m.authorized = obj.find("\"authorized\":true") != std::string::npos;
            size_t a = obj.find("\"ipAssignments\":[");
            if (a != std::string::npos) {
                a = obj.find('[', a);
                a = (a == std::string::npos) ? std::string::npos : obj.find('"', a);
                size_t b = (a == std::string::npos) ? std::string::npos : obj.find('"', a + 1);
                if (b != std::string::npos) {
                    std::string ip = obj.substr(a + 1, b - a - 1);
                    if (ip.find('.') != std::string::npos) m.ip = ip;
                }
            }
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
    size_t p = 0;
    while ((p = resp.find('"', p)) != std::string::npos) {
        size_t e = resp.find('"', p + 1);
        if (e == std::string::npos) break;
        std::string id = resp.substr(p + 1, e - p - 1);
        p = e + 1;
        if (id.size() == 16) out.push_back(id);
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
    /* Every controller would otherwise hand out 10.147.17.0/24, which turns
     * into duplicate interface addresses when one device joins two of our
     * networks. Derive a stable /24 from the controller address instead. */
    unsigned x = (unsigned)strtoul(controller_id.substr(0, 2).c_str(), nullptr, 16);
    unsigned y = (unsigned)strtoul(controller_id.substr(2, 2).c_str(), nullptr, 16);
    std::string base = "10." + std::to_string(128 + (x % 96))
                            + "." + std::to_string(1 + (y % 254));
    std::string body =
        "{\"name\":\"dArkOS Netplay\",\"private\":true,\"enableBroadcast\":true,"
        "\"v4AssignMode\":{\"zt\":true},"
        "\"ipAssignmentPools\":[{\"ipRangeStart\":\"" + base + ".1\",\"ipRangeEnd\":\"" + base + ".254\"}],"
        "\"routes\":[{\"target\":\"" + base + ".0/24\"}]}";
    std::string resp, err;
    if (!zt_api("POST", "/controller/network/" + controller_id + "______", body, resp, err))
        return "";
    std::string nwid = json_string_field(resp, "nwid");
    if (nwid.empty()) nwid = json_string_field(resp, "id");
    return nwid;
}
