#include "lobby.h"

#include <cctype>
#include <curl/curl.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <map>
#include <net/if.h>
#include <netinet/in.h>
#include <SDL2/SDL.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "config.h"
#include "util.h"

const char *LOBBY_URL = "http://lobby.libretro.com/list";

// netplay_host_method: 0 unknown, 1 manual, 2 upnp, 3 MITM.
bool is_mitm(const Room &rm) { return rm.host_method == 3 || !rm.mitm_ip.empty(); }


// RetroArch LAN discovery: 4-byte query to UDP 55435, host answers with a
// fixed 688-byte ad_packet (network byte order, strings NUL-padded).
#define LAN_DISCOVERY_PORT 55435u
#define LAN_QUERY_MAGIC  0x52414E51u  /* RANQ */
#define LAN_RESPONSE_MAGIC 0x52414E53u /* RANS */
struct LanAdPacket {
    uint32_t header;
    int32_t  crc;
    int32_t  port;
    uint32_t has_password;
    char nick[32];
    char frontend[32];
    char core[32];
    char core_version[32];
    char retroarch_version[32];
    char content[256];
    char subsystem_name[256];
};
static_assert(sizeof(LanAdPacket) == 688, "ad_packet layout must match RetroArch");

static std::string fixed_str(const char *p, size_t n) {
    size_t l = 0;
    while (l < n && p[l]) l++;
    return std::string(p, l);
}

// Returns hosts found within `wait_ms`; addresses are the responders' LAN IPs.
void lan_discover(std::vector<Room> &out, int wait_ms, const std::atomic<bool> *cancel) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    struct sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(LAN_DISCOVERY_PORT);
    to.sin_addr.s_addr = INADDR_BROADCAST;
    uint32_t magic = htonl(LAN_QUERY_MAGIC);
    sendto(fd, &magic, sizeof(magic), 0, (struct sockaddr *)&to, sizeof(to));

    /* Also query each interface's subnet broadcast so a ZeroTier (or USB
     * tether) virtual LAN is covered, not just the default route. */
    struct ifaddrs *ifs = nullptr;
    if (getifaddrs(&ifs) == 0) {
        for (struct ifaddrs *i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
            if ((i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
            if (!i->ifa_broadaddr) continue;
            struct sockaddr_in dst = to;
            dst.sin_addr = ((struct sockaddr_in *)i->ifa_broadaddr)->sin_addr;
            if (!dst.sin_addr.s_addr) continue;
            sendto(fd, &magic, sizeof(magic), 0, (struct sockaddr *)&dst, sizeof(dst));
        }
        freeifaddrs(ifs);
    }

    Uint32 start = SDL_GetTicks();
    while ((int)(SDL_GetTicks() - start) < wait_ms) {
        if (cancel && cancel->load()) break;
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(fd, &rf);
        struct timeval tv = { 0, 150000 };
        int rv = select(fd + 1, &rf, nullptr, nullptr, &tv);
        if (rv <= 0) continue;
        LanAdPacket pkt;
        struct sockaddr_in from = {};
        socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &fl);
        if (n != (ssize_t)sizeof(pkt)) continue;
        if (ntohl(pkt.header) != LAN_RESPONSE_MAGIC) continue;
        int port = (int)ntohl((uint32_t)pkt.port);
        if (port <= 0 || port > 65535) continue;

        Room rm;
        rm.address = inet_ntoa(from.sin_addr);
        rm.port = port;
        rm.gamename = fixed_str(pkt.content, sizeof(pkt.content));
        uint32_t crc = ntohl((uint32_t)pkt.crc);
        char crcbuf[16];
        snprintf(crcbuf, sizeof(crcbuf), "%08X", crc);
        rm.gamecrc = crcbuf;
        rm.corename = fixed_str(pkt.core, sizeof(pkt.core));
        rm.coreversion = fixed_str(pkt.core_version, sizeof(pkt.core_version));
        rm.nickname = fixed_str(pkt.nick, sizeof(pkt.nick));
        rm.frontend = fixed_str(pkt.frontend, sizeof(pkt.frontend));
        rm.retroarch_version = fixed_str(pkt.retroarch_version, sizeof(pkt.retroarch_version));
        rm.has_password = (ntohl(pkt.has_password) & 1) != 0;
        rm.connectable = true;
        rm.lan = true;
        rm.host_method = 0;
        if (!rm.gamename.empty() && rm.gamename != "N/A") out.push_back(rm);
    }
    close(fd);
}

#include "json.h"

// Each list entry is {"fields": {..}} (some API versions serve it flat), so
// nested objects are merged one level deep into the same field map.
static void read_object(JsonReader &r, std::map<std::string, std::string> &f) {
    r.eat('{');
    while (true) {
        r.ws();
        if (r.i >= r.s.size() || r.s[r.i] == '}') { r.i++; break; }
        std::string k;
        if (!r.str(k) || !r.eat(':')) break;
        r.ws();
        if (r.i < r.s.size() && r.s[r.i] == '{') {
            std::map<std::string, std::string> sub;
            read_object(r, sub);
            for (auto &kv : sub) f[kv.first] = kv.second;
        } else if (r.i < r.s.size() && r.s[r.i] == '[') {
            r.skip_value();
        } else if (r.i < r.s.size() && r.s[r.i] == '"') {
            std::string v; r.str(v); f[k] = v;
        } else if (r.s.compare(r.i, 4, "true") == 0) { f[k] = "true"; r.i += 4; }
        else if (r.s.compare(r.i, 5, "false") == 0) { f[k] = "false"; r.i += 5; }
        else if (r.s.compare(r.i, 4, "null") == 0) { r.i += 4; }
        else { size_t st = r.i; while (r.i < r.s.size() && r.s[r.i] != ',' && r.s[r.i] != '}') r.i++; f[k] = r.s.substr(st, r.i - st); }
        r.ws();
        if (!r.eat(',')) { if (r.eat('}')) break; }
    }
}

std::vector<Room> parse_lobby(const std::string &json) {
    std::vector<Room> rooms;
    JsonReader r(json);
    if (!r.eat('[')) return rooms;
    while (true) {
        r.ws();
        if (r.i >= json.size() || json[r.i] == ']') break;
        if (json[r.i] != '{') break;
        std::map<std::string, std::string> f;
        read_object(r, f);
        Room room;
        auto get = [&](const char *k) { auto it = f.find(k); return it == f.end() ? std::string() : it->second; };
        room.gamename = get("game_name");
        room.gamecrc = get("game_crc");
        room.corename = get("core_name");
        room.coreversion = get("core_version");
        room.nickname = get("username");
        if (room.nickname.empty()) room.nickname = get("nickname");
        room.country = get("country");
        room.address = get("ip");
        room.mitm_ip = get("mitm_ip");
        room.mitm_session = get("mitm_session");
        room.retroarch_version = get("retroarch_version");
        room.frontend = get("frontend");
        room.port = atoi(get("port").c_str());
        room.mitm_port = atoi(get("mitm_port").c_str());
        room.players = atoi(get("player_count").c_str());
        if (room.players < 0) room.players = 0;  // -1 = unknown
        room.spectators = atoi(get("spectator_count").c_str());
        if (room.spectators < 0) room.spectators = 0;
        room.host_method = atoi(get("host_method").c_str());
        room.has_password = get("has_password") == "true";
        // Entries that are no longer listed as connectable keep the port but
        // must not be offered for joining.
        room.connectable = get("connectable") != "false";
        room.lan = get("lan") == "true";
        if (!room.gamename.empty() && room.port > 0 && !get("game_name").empty()) rooms.push_back(room);
        r.ws();
        if (r.i >= json.size()) break;
        if (!r.eat(',')) break;
    }
    return rooms;
}

static int curl_cancelled(void *ud, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return (ud && ((const std::atomic<bool> *)ud)->load()) ? 1 : 0;
}

bool http_get(const std::string &url, std::string &out, std::string &err,
              const std::atomic<bool> *cancel) {
    CURL *c = curl_easy_init();
    if (!c) { err = "curl init"; return false; }
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "dArkOS-ra-lobby/1.0");
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);  // same as RA's list fetch on KMS images
    // The lobby host (lobby.relay.retroarch.com) is often unreachable without a
    // proxy here; PROXY= in ra-lobby.cfg or the usual *_proxy env vars win.
    if (!proxy.empty()) {
        curl_easy_setopt(c, CURLOPT_PROXY, proxy.c_str());
    } else {
        for (const char *n : { "HTTPS_PROXY", "https_proxy", "ALL_PROXY", "all_proxy" }) {
            const char *v = getenv(n);
            if (v && *v) { curl_easy_setopt(c, CURLOPT_PROXY, v); break; }
        }
    }
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    if (cancel) {
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, curl_cancelled);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, (void *)cancel);
    }
    CURLcode rc = curl_easy_perform(c);
    if (rc == CURLE_ABORTED_BY_CALLBACK) err = "cancelled";
    else if (rc != CURLE_OK) err = curl_easy_strerror(rc);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) return false;
    if (code != 200) { err = "HTTP " + std::to_string(code); return false; }
    return true;
}
