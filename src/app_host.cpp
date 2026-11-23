#include "app.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <unistd.h>

#include "config.h"
#include "i18n.h"
#include "launch.h"
#include "theme.h"
#include "util.h"

void App::build_systems() {
    host.systems.clear();
    for (EsSystem &s : load_es_systems("/etc/emulationstation/es_systems.cfg")) {
        auto add = [&](const std::string &c, bool is64) {
            if (c.empty()) return;
            if (std::find(s.hostable.begin(), s.hostable.end(), c) != s.hostable.end()) return;
            auto it = cores.find(c);
            if (it == cores.end()) return;
            if (is64 ? it->second.path64.empty() : it->second.path32.empty()) return;
            if (!core_info_netplay(c, is64)) return;
            s.hostable.push_back(c);
        };
        for (auto &c : s.cores64) add(c, true);
        for (auto &c : s.cores32) add(c, false);
        if (s.hostable.empty()) continue;
        std::string rel = s.path;
        for (const char *pre : { "/roms2/", "/roms/" })
            if (rel.rfind(pre, 0) == 0) { rel = rel.substr(strlen(pre)); break; }
        if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
        bool have = false;
        for (const char *root : { "/roms", "/roms2" }) {
            std::string dir = std::string(root) + "/" + rel;
            auto it = host.dir_cache.find(dir);
            if (it == host.dir_cache.end())
                it = host.dir_cache.emplace(dir, dir_has_game(dir, s.exts, 0)).first;
            if (it->second) { have = true; break; }
        }
        if (!have && access(s.path.c_str(), R_OK) == 0) {
            auto it = host.dir_cache.find(s.path);
            if (it == host.dir_cache.end())
                it = host.dir_cache.emplace(s.path, dir_has_game(s.path, s.exts, 0)).first;
            have = it->second;
        }
        if (!have) continue;
        host.systems.push_back(s);
    }
    std::sort(host.systems.begin(), host.systems.end(), [](const EsSystem &a, const EsSystem &b) {
        return lower(a.fullname.empty() ? a.name : a.fullname) <
               lower(b.fullname.empty() ? b.name : b.fullname);
    });
}


void App::build_sys_games() {
    host.games.clear();
    if (host.sys < 0) return;
    const EsSystem &s = host.systems[host.sys];
    std::string rel = s.path;
    for (const char *pre : { "/roms2/", "/roms/" })
        if (rel.rfind(pre, 0) == 0) { rel = rel.substr(strlen(pre)); break; }
    if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
    if (!rel.empty() && rel.back() == '/') rel.pop_back();
    for (const char *root : { "/roms", "/roms2" })
        scan_games_dir(std::string(root) + "/" + rel, s.exts, 0, host.games);
    std::sort(host.games.begin(), host.games.end(), [](const GameEntry &a, const GameEntry &b) {
        return a.path < b.path;
    });
    host.games.erase(std::unique(host.games.begin(), host.games.end(),
        [](const GameEntry &a, const GameEntry &b) { return a.path == b.path; }), host.games.end());
    std::sort(host.games.begin(), host.games.end(), [](const GameEntry &a, const GameEntry &b) {
        return lower(a.name) < lower(b.name);
    });
}

void App::select_system(int idx) {
    host.sys = idx;
    const EsSystem &s = host.systems[idx];
    host.cores = s.hostable;
    host.core = host.cores.empty() ? "" : host.cores[0];
    host.game_name.clear();
    host.game_path.clear();
    build_sys_games();
}

std::string App::system_alias() const {
    if (host.sys < 0) return "";
    const EsSystem &s = host.systems[host.sys];
    return s.fullname.empty() ? s.name : s.fullname;
}

int App::picker_count() const {
    if (host.pick_kind == 0) return (int)host.systems.size();
    if (host.pick_kind == 1) return (int)host.games.size();
    return (int)host.cores.size();
}

void App::picker_move(int d) {
    int n = picker_count();
    if (n == 0) return;
    host.pick_sel = std::max(0, std::min(n - 1, host.pick_sel + d));
    int vis = std::max(3, (SH - 220) / 44);
    if (host.pick_sel < host.pick_scroll) host.pick_scroll = host.pick_sel;
    if (host.pick_sel >= host.pick_scroll + vis) host.pick_scroll = host.pick_sel - vis + 1;
}

void App::picker_choose() {
    if (host.pick_kind == 0) {
        if (host.pick_sel < (int)host.systems.size()) select_system(host.pick_sel);
    } else if (host.pick_kind == 1) {
        if (host.pick_sel < (int)host.games.size()) {
            host.game_name = host.games[host.pick_sel].name;
            host.game_path = host.games[host.pick_sel].path;
        }
    } else if (host.pick_kind == 2) {
        if (host.pick_sel < (int)host.cores.size()) host.core = host.cores[host.pick_sel];
    }
    screen = 3;
}

void App::host_move(int d) { host.row = std::max(0, std::min(7, host.row + d)); }

void App::host_change() {
    switch (host.row) {
    case 0:
    case 1:
    case 2: {
        if (host.row >= 1 && host.sys < 0) { show_toast(L("先选择系统", "Pick a system first")); break; }
        host.pick_kind = host.row;
        host.pick_sel = 0;
        host.pick_scroll = 0;
        screen = 4;
        break;
    }
    case 3:
        if (host.lan_only) { show_toast(L("仅局域网模式下固定直连", "LAN-only forces direct")); break; }
        open_settings(); set.sel = 1;
        break;
    case 4:
        if (host.lan_only) { show_toast(L("仅局域网模式不公开", "LAN-only is not announced")); break; }
        announce = !announce;
        break;
    case 5: host.lan_only = !host.lan_only; break;
    case 6:
        if (host.pass_on) { host.pass_on = false; host.pass.clear(); }
        else { open_editor(4); set.buf.clear(); }
        break;
    case 7: host_launch(); break;
    }
}

void App::host_launch() {
    if (host.game_path.empty()) { show_toast(L("先选择游戏", "Pick a game first")); return; }
    if (host.core.empty() || !cores.count(host.core)) { show_toast(L("先选择核心", "Pick a core first")); return; }
    const CoreRef &cr = cores[host.core];
    bool is64 = !cr.path64.empty();
    std::string core = is64 ? cr.path64 : cr.path32;
    if (core.empty()) { show_toast(L("核心不可用", "Core unavailable")); return; }
    launch_host(core, is64, host.game_path, host.lan_only, host.pass_on ? host.pass : std::string());
}

void App::request_join(const Room &rm) {
    if (!rm.has_password) { join_pass.clear(); join_room(rm, ""); return; }
    std::string c32; std::string core = find_core(rm.corename, cores, c32);
    LocalGame lg = find_game(rm.gamename, rm.gamecrc, names, crcs);
    if (!rm.connectable) { show_toast(L("房间不可连接", "Not connectable")); return; }
    if (core.empty()) { show_toast(L("缺少模拟核心", "Emulator core missing")); return; }
    if (!lg.found) { show_toast(L("本地没有这个游戏", "Game not found locally")); return; }
    pending_room = rm;
    has_pending = true;
    join_pass.clear();
    open_editor(3);
}

void App::join_room(const Room &rm, const std::string &pass) {
    std::string c32; std::string core = find_core(rm.corename, cores, c32);
    LocalGame lg = find_game(rm.gamename, rm.gamecrc, names, crcs);
    if (!rm.connectable) { show_toast(L("房间不可连接", "Not connectable")); return; }
    if (core.empty()) { show_toast(L("缺少模拟核心", "Emulator core missing")); return; }
    if (!lg.found) { show_toast(L("本地没有这个游戏", "Game not found locally")); return; }
    if (!ra_version_at_least(rm.retroarch_version, 1, 9, 1)) {
        show_toast(L("对方 RetroArch 版本过低 (需 >=1.9.1)", "Host RetroArch too old (need >=1.9.1)"));
        return;
    }
    launch_join(core, lg.path, rm, pass);
}
