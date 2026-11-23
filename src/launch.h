#pragma once

#include <string>
#include <vector>

struct Room;

// Netplay protocol by release (netplay_protocol.h, LOW=5 throughout):
//   1.10.0 - 1.20.0 -> 6
//   1.21.0 - 1.22.2 -> 7
//   Nov-2026 build  -> 8 (analog joypads + lockstep are protocol-8 only)
// Protocol 8 hosts reject older clients while analog joypads is on, and older
// peers crash on the analog device bit, so hosts/older peers get a launch-only
// append config with both switches off. The user's main retroarch.cfg is not
// modified.
bool ra_version_at_least(const std::string &v, int maj, int min, int pat);

// Writes /tmp/ra-lobby-netplay.cfg; returns "--appendconfig=..." or "".
std::string netplay_compat_args(const std::string &peer_version,
                                const std::string &pass, int port);

// /home/ark/.config/ra-lobby-debug enables RA's verbose file log.
void add_ra_debug(std::vector<std::string> &args);

[[noreturn]] void launch_host(const std::string &core, bool is64,
                              const std::string &game, bool lan_only,
                              const std::string &pass);
[[noreturn]] void launch_join(const std::string &core, const std::string &game,
                              const Room &rm, const std::string &pass);
