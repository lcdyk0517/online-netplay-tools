#pragma once

#include <string>
#include <vector>

struct ZtStatus {
    std::string node, state, ip;
    bool found = false;   /* network listed */
    bool ok    = false;   /* status OK */
};

struct ZtMember {
    std::string id, ip;
    bool authorized = false;
};

bool zt_installed();
bool zt_tun_ok();
bool zt_daemon_up();
bool zt_tun_ensure();
bool zt_ensure_daemon();
std::string zt_cli(const std::string &args);
bool zt_valid_id(const std::string &id);
ZtStatus zt_status(const std::string &netid);

// local self-hosted controller (no my.zerotier.com)
std::vector<ZtMember> zt_fetch_members(const std::string &nwid);
bool zt_delete_member(const std::string &nwid, const std::string &id);
std::vector<std::string> zt_controller_networks();
bool zt_authorize_member(const std::string &nwid, const std::string &id, bool auth);
std::string zt_create_network(const std::string &controller_id);
