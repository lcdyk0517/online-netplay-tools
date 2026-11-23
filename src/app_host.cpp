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
    systems.clear();
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
            auto it = dir_has_cache.find(dir);
            if (it == dir_has_cache.end())
                it = dir_has_cache.emplace(dir, dir_has_game(dir, s.exts, 0)).first;
            if (it->second) { have = true; break; }
        }
        if (!have && access(s.path.c_str(), R_OK) == 0) {
            auto it = dir_has_cache.find(s.path);
            if (it == dir_has_cache.end())
                it = dir_has_cache.emplace(s.path, dir_has_game(s.path, s.exts, 0)).first;
            have = it->second;
        }
        if (!have) continue;
        systems.push_back(s);
    }
    std::sort(systems.begin(), systems.end(), [](const EsSystem &a, const EsSystem &b) {
        return lower(a.fullname.empty() ? a.name : a.fullname) <
               lower(b.fullname.empty() ? b.name : b.fullname);
    });
}


void App::build_sys_games() {
    sys_games.clear();
    if (hsys < 0) return;
    const EsSystem &s = systems[hsys];
    std::string rel = s.path;
    for (const char *pre : { "/roms2/", "/roms/" })
        if (rel.rfind(pre, 0) == 0) { rel = rel.substr(strlen(pre)); break; }
    if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
    if (!rel.empty() && rel.back() == '/') rel.pop_back();
    for (const char *root : { "/roms", "/roms2" })
        scan_games_dir(std::string(root) + "/" + rel, s.exts, 0, sys_games);
    std::sort(sys_games.begin(), sys_games.end(), [](const GameEntry &a, const GameEntry &b) {
        return a.path < b.path;
    });
    sys_games.erase(std::unique(sys_games.begin(), sys_games.end(),
        [](const GameEntry &a, const GameEntry &b) { return a.path == b.path; }), sys_games.end());
    std::sort(sys_games.begin(), sys_games.end(), [](const GameEntry &a, const GameEntry &b) {
        return lower(a.name) < lower(b.name);
    });
}

void App::select_system(int idx) {
    hsys = idx;
    const EsSystem &s = systems[idx];
    sys_cores = s.hostable;
    hcore = sys_cores.empty() ? "" : sys_cores[0];
    hgame_name.clear();
    hgame_path.clear();
    build_sys_games();
}

std::string App::system_alias() const {
    if (hsys < 0) return "";
    const EsSystem &s = systems[hsys];
    return s.fullname.empty() ? s.name : s.fullname;
}

int App::picker_count() const {
    if (pick_kind == 0) return (int)systems.size();
    if (pick_kind == 1) return (int)sys_games.size();
    return (int)sys_cores.size();
}

void App::picker_move(int d) {
    int n = picker_count();
    if (n == 0) return;
    pick_sel = std::max(0, std::min(n - 1, pick_sel + d));
    int vis = std::max(3, (SH - 220) / 44);
    if (pick_sel < pick_scroll) pick_scroll = pick_sel;
    if (pick_sel >= pick_scroll + vis) pick_scroll = pick_sel - vis + 1;
}

void App::picker_choose() {
    if (pick_kind == 0) {
        if (pick_sel < (int)systems.size()) select_system(pick_sel);
    } else if (pick_kind == 1) {
        if (pick_sel < (int)sys_games.size()) {
            hgame_name = sys_games[pick_sel].name;
            hgame_path = sys_games[pick_sel].path;
        }
    } else if (pick_kind == 2) {
        if (pick_sel < (int)sys_cores.size()) hcore = sys_cores[pick_sel];
    }
    screen = 3;
}

void App::host_move(int d) { hsel = std::max(0, std::min(7, hsel + d)); }

void App::host_change() {
    switch (hsel) {
    case 0:
    case 1:
    case 2: {
        if (hsel >= 1 && hsys < 0) { show_toast(L("先选择系统", "Pick a system first")); break; }
        pick_kind = hsel;
        pick_sel = 0;
        pick_scroll = 0;
        screen = 4;
        break;
    }
    case 3:
        if (lan_only) { show_toast(L("仅局域网模式下固定直连", "LAN-only forces direct")); break; }
        open_settings(); ssel = 1;
        break;
    case 4:
        if (lan_only) { show_toast(L("仅局域网模式不公开", "LAN-only is not announced")); break; }
        announce = !announce;
        break;
    case 5: lan_only = !lan_only; break;
    case 6:
        if (host_pass_on) { host_pass_on = false; host_pass.clear(); }
        else { open_editor(4); edit_buf.clear(); }
        break;
    case 7: host_launch(); break;
    }
}

void App::host_launch() {
    if (hgame_path.empty()) { show_toast(L("先选择游戏", "Pick a game first")); return; }
    if (hcore.empty() || !cores.count(hcore)) { show_toast(L("先选择核心", "Pick a core first")); return; }
    const CoreRef &cr = cores[hcore];
    bool is64 = !cr.path64.empty();
    std::string core = is64 ? cr.path64 : cr.path32;
    if (core.empty()) { show_toast(L("核心不可用", "Core unavailable")); return; }
    launch_host(core, is64, hgame_path, lan_only, host_pass_on ? host_pass : std::string());
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
