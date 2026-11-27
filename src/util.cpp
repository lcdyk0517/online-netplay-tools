#include "util.h"

#include <cstdio>
#include <string>

#include <curl/curl.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>

std::string trim_str(std::string s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}


std::string lower(std::string s) {
    for (auto &c : s) c = (char)tolower((unsigned char)c);
    return s;
}
std::string norm(std::string s) {
    std::string o;
    for (char c : lower(s)) if (isalnum((unsigned char)c)) o += c;
    return o;
}
bool ends_with(const std::string &s, const std::string &e) {
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}
std::string base_stem(const std::string &p) {
    size_t slash = p.find_last_of('/');
    std::string b = slash == std::string::npos ? p : p.substr(slash + 1);
    size_t dot = b.find_last_of('.');
    return dot == std::string::npos ? b : b.substr(0, dot);
}

size_t curl_write(char *p, size_t sz, size_t n, void *ud) {
    ((std::string *)ud)->append(p, sz * n);
    return sz * n;
}

std::string shq(const std::string &s) {
    std::string o = "'";
    for (char c : s) {
        if (c == '\'') o += "'\\''";
        else o += c;
    }
    o += "'";
    return o;
}

int tcp_latency_ms(const std::string &hostport, int timeout_ms) {
    size_t bar = hostport.find('|');
    if (bar == std::string::npos) return -1;
    std::string host = trim_str(hostport.substr(0, bar));
    std::string ps = trim_str(hostport.substr(bar + 1));
    if (host.empty() || ps.empty()) return -1;
    char *end = nullptr;
    long port = strtol(ps.c_str(), &end, 10);
    if (!end || *end || port <= 0 || port > 65535) return -1;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%ld", port);
    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    if (getaddrinfo(host.c_str(), portstr, &hints, &res) != 0) return -1;

    int best = -1;
    for (int attempt = 0; attempt < 2 && best < 0; attempt++)
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0) continue;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        auto t0 = std::chrono::steady_clock::now();
        int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            struct pollfd pfd = { fd, POLLOUT, 0 };
            rc = poll(&pfd, 1, timeout_ms);
            if (rc > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0)
                    rc = 0;
                else
                    rc = -1;
            } else {
                rc = -1;
            }
        }
        if (rc == 0) {
            int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            close(fd);
            if (best < 0 || ms < best) best = ms;
            break;
        }
        close(fd);
    }
    freeaddrinfo(res);
    return best;
}

static bool lobby_line(const std::string &body, const char *key, std::string &out) {
    size_t at = 0;
    const std::string k = std::string(key) + "=";
    while (at < body.size()) {
        size_t e = body.find('\n', at);
        std::string line = body.substr(at, e == std::string::npos ? std::string::npos : e - at);
        if (line.rfind(k, 0) == 0) {
            out = trim_str(line.substr(k.size()));
            return !out.empty();
        }
        if (e == std::string::npos) break;
        at = e + 1;
    }
    return false;
}

bool lobby_tunnel(const std::string &handle, std::string &addr, int &port) {
    static bool inited = false;
    if (!inited) { curl_global_init(CURL_GLOBAL_DEFAULT); inited = true; }
    CURL *c = curl_easy_init();
    if (!c) return false;
    std::string body;
    std::string url = "http://lobby.libretro.com/tunnel?name=" + handle;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "online-netplay-tools");
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || status != 200) return false;
    std::string ps;
    if (!lobby_line(body, "tunnel_addr", addr) || !lobby_line(body, "tunnel_port", ps)) return false;
    char *end = nullptr;
    long v = strtol(ps.c_str(), &end, 10);
    if (!end || *end || v <= 0 || v > 65535) return false;
    port = (int)v;
    return true;
}
