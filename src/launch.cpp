#include "launch.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>

#include "config.h"
#include "lobby.h"

// Netplay protocol by release (netplay_protocol.h, LOW=5 throughout):
//   1.10.0 - 1.20.0 -> 6
//   1.21.0 - 1.22.2 -> 7
//   Nov-2026 build  -> 8 (analog joypads + lockstep are protocol-8 only)
// Protocol 8 hosts reject older clients while analog joypads is on, and
// older peers crash on the analog device bit, so hosts/older peers get a
// launch-only append config with both switches off. The user's main
// retroarch.cfg is not modified.
static void version_parts(const std::string &v, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    sscanf(v.c_str(), "%d.%d.%d", &out[0], &out[1], &out[2]);
}
bool ra_version_at_least(const std::string &v, int maj, int min, int pat) {
    if (v.empty()) return true;
    int p[3];
    version_parts(v, p);
    if (p[0] != maj) return p[0] > maj;
    if (p[1] != min) return p[1] > min;
    return p[2] >= pat;
}
// Writes /tmp/ra-lobby-netplay.cfg; returns "--appendconfig=..." or "".
std::string netplay_compat_args(const std::string &peer_version,
                                       const std::string &pass, int port) {
    /* Our own build still reports 1.22.2 while speaking protocol 8, so a
     * peer version >= 1.23 is the only safe "new peer" signal. Everything
     * else (including 1.22.2) takes the compatible path. */
    bool new_peer = ra_version_at_least(peer_version, 1, 23, 0);
    std::string pw;
    for (char c : pass) if (c != '"' && c != '\\') pw += c;
    if (new_peer && pw.empty()) return "";
    const char *path = "/tmp/ra-lobby-netplay.cfg";
    std::ofstream out(path);
    if (!out) return "";
    if (!new_peer) {
        out << "netplay_analog_joypads = \"false\"\n";
        out << "netplay_lockstep = \"false\"\n";
    }
    if (!pw.empty()) out << "netplay_password = \"" << pw << "\"\n";
    if (port > 0) out << "netplay_ip_port = \"" << port << "\"\n";
    out.close();
    chmod(path, 0600);
    return std::string("--appendconfig=") + path;
}

// /home/ark/.config/ra-lobby-debug enables RA's verbose file log.
void add_ra_debug(std::vector<std::string> &args) {
    if (access("/home/ark/.config/ra-lobby-debug", F_OK) != 0) return;
    args.insert(args.begin() + 1, "-v");
    args.insert(args.begin() + 2, "--log-file=/tmp/ra-lobby-ra.log");
}


[[noreturn]] void launch_host(const std::string &core, bool is64,
                              const std::string &game, bool lan_only,
                              const std::string &pass) {
    std::string bin = is64 ? "/opt/retroarch/bin/retroarch" : "/opt/retroarch/bin/retroarch32";
    std::string cfg = is64 ? RA_CFG64 : RA_CFG32;
    save_ra_all();  // relay + announce settings apply to the host
    std::string port = "55435";
    std::string v;
    if (cfg_get(cfg.c_str(), "netplay_ip_port", v) && !v.empty()) port = v;
    std::vector<std::string> args = { bin, "-c", cfg, "-L", core, game, "-H",
        "--port=" + port, "--nick=" + nick };
    const char *np_path = "/tmp/ra-lobby-netplay.cfg";
    {
        std::ofstream np(np_path);
        if (np) {
            /* host always takes the protocol-7 path for older clients */
            np << "netplay_analog_joypads = \"false\"\n";
            np << "netplay_lockstep = \"false\"\n";
            np << "netplay_ip_port = \"" << port << "\"\n";
            if (!pass.empty()) {
                std::string pw;
                for (char c : pass) if (c != '"' && c != '\\') pw += c;
                if (!pw.empty()) np << "netplay_password = \"" << pw << "\"\n";
            }
            if (lan_only) {
                np << "netplay_public_announce = \"false\"\n";
                np << "netplay_use_mitm_server = \"false\"\n";
                np << "netplay_nat_traversal = \"false\"\n";
            }
        }
        args.push_back(std::string("--appendconfig=") + np_path);
    }
    add_ra_debug(args);
    SDL_Quit();
    std::vector<char *> av;
    for (auto &a : args) av.push_back(const_cast<char *>(a.c_str()));
    av.push_back(nullptr);
    execv(bin.c_str(), av.data());
    fprintf(stderr, "exec %s failed\n", bin.c_str());
    _exit(1);
}

[[noreturn]] void launch_join(const std::string &core, const std::string &game,
                              const Room &rm, const std::string &pass) {
    bool is64 = core.find("retroarch32") == std::string::npos;
    std::string bin = is64 ? "/opt/retroarch/bin/retroarch" : "/opt/retroarch/bin/retroarch32";
    std::string cfg = is64 ? RA_CFG64 : RA_CFG32;
    // MITM rooms join the relay, not the host's own address.
    std::string host = rm.address;
    int port = rm.port;
    if (is_mitm(rm) && !rm.mitm_ip.empty()) { host = rm.mitm_ip; port = rm.mitm_port; }
    std::vector<std::string> args = { bin, "-c", cfg, "-L", core, game, "-C", host,
        "--port=" + std::to_string(port), "--nick=" + nick };
    if (is_mitm(rm) && !rm.mitm_session.empty()) args.push_back("--mitm-session=" + rm.mitm_session);
    std::string npcfg = netplay_compat_args(rm.retroarch_version, pass, rm.port);
    if (!npcfg.empty()) args.push_back(npcfg);
    add_ra_debug(args);
    SDL_Quit();
    std::vector<char *> av;
    for (auto &a : args) av.push_back(const_cast<char *>(a.c_str()));
    av.push_back(nullptr);
    execv(bin.c_str(), av.data());
    fprintf(stderr, "exec %s failed\n", bin.c_str());
    _exit(1);
}
