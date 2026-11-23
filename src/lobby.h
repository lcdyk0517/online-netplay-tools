#pragma once

#include <atomic>
#include <string>
#include <vector>

struct Room {
    std::string gamename, gamecrc, corename, coreversion, nickname, country;
    std::string address, mitm_ip, mitm_session, retroarch_version, frontend;
    int port = 0, mitm_port = 0, players = 0, spectators = 0, host_method = 0;
    bool has_password = false, connectable = true, lan = false;
};

// The lobby host throttles HTTPS here; plain HTTP answers instantly and the
// list is public data.
extern const char *LOBBY_URL;

// netplay_host_method: 0 unknown, 1 manual, 2 upnp, 3 MITM.
bool is_mitm(const Room &rm);

bool http_get(const std::string &url, std::string &out, std::string &err,
              const std::atomic<bool> *cancel = nullptr);
std::vector<Room> parse_lobby(const std::string &json);

// RetroArch LAN discovery: 4-byte query to UDP 55435, host answers with a
// fixed 688-byte ad_packet. Returns hosts found within `wait_ms`.
void lan_discover(std::vector<Room> &out, int wait_ms = 1200,
                  const std::atomic<bool> *cancel = nullptr);
