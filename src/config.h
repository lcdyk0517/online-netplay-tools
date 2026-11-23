#pragma once

#include <string>

extern const char *RA_CFG64;
extern const char *RA_CFG32;

// netplay settings mirrored from RetroArch (64-bit config wins on load, both
// files are written on save)
extern std::string nick;
extern bool use_mitm;
extern std::string mitm_server;
extern std::string custom_mitm;
extern bool announce;

// ra-lobby.cfg
extern std::string proxy;
extern std::string zt_net;   // network in use now
extern std::string zt_own;   // network created on this device
extern bool zt_host;         // this device is the controller

// Optional short-code lookup service base URL (empty = disabled) and the code
// last published for zt_own.
extern std::string short_url;
extern std::string zt_short;
extern long long zt_short_exp;   // unix time when zt_short stops resolving

bool cfg_get(const char *path, const std::string &key, std::string &out);
void cfg_set(const char *path, const std::string &key, const std::string &value);

void load_cfg();
bool save_cfg();
void load_ra_all();
void save_ra_all();
