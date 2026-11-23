#include "app.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "config.h"
#include "i18n.h"
#include "theme.h"
#include "util.h"

void App::open_settings() {
    screen = 1;
    set.sel = 0;
    zt.st = ZtStatus();
    zt.members.clear();
    refresh_zt_async();
}

int App::relay_idx() const {
    if (!use_mitm) return 0;
    static const char *presets[4] = { "nyc", "madrid", "saopaulo", "singapore" };
    for (int i = 0; i < 4; i++) if (mitm_server == presets[i]) return i + 1;
    return 5;
}

void App::set_relay_idx(int i) {
    static const char *presets[4] = { "nyc", "madrid", "saopaulo", "singapore" };
    if (i == 0) use_mitm = false;
    else if (i <= 4) { use_mitm = true; mitm_server = presets[i - 1]; }
    else { use_mitm = true; mitm_server = "custom"; }
}

std::string App::relay_label(int i) const {
    switch (i) {
    case 0: return L("直连", "Direct");
    case 1: return L("纽约", "New York");
    case 2: return L("马德里", "Madrid");
    case 3: return L("圣保罗", "Sao Paulo");
    case 4: return L("新加坡", "Singapore");
    default: return L("自定义", "Custom");
    }
}

std::vector<int> App::settings_rows() const {
    std::vector<int> v{ 0, 1 };
    if (relay_idx() == 5) v.push_back(2);
    v.push_back(3);
    v.push_back(4);
    v.push_back(5);
    v.push_back(6);
    v.push_back(9);   /* join a network, right below members/create */
    v.push_back(7);
    v.push_back(8);
    return v;
}

void App::settings_move(int d) {
    std::vector<int> rs = settings_rows();
    int i = 0;
    for (; i < (int)rs.size(); i++) if (rs[i] == set.sel) break;
    i = std::max(0, std::min((int)rs.size() - 1, i + d));
    set.sel = rs[i];
}

void App::open_editor(int target) {
    set.target = target;
    set.buf = target == 0 ? nick : (target == 1 ? custom_mitm : (target == 2 ? zt_net : (target == 3 ? join_pass : host.pass)));
    set.row = 0; set.col = 0; screen = 2;
}

void App::settings_change() {
    switch (set.sel) {
    case 0: open_editor(0); break;
    case 1: set_relay_idx((relay_idx() + 1) % 6); break;
    case 2: open_editor(1); break;
    case 3: announce = !announce; break;
    case 4:
        if (zt_host) show_toast(L("本机网络，成员在成员管理里", "Hosted here; see Members"));
        else open_editor(2);
        break;
    case 5: {
        /* On a host this row always means "our own network"; joining some
         * other network has its own entry. Never ask for a known ID. */
        std::string net = (zt_host && zt_valid_id(zt_own)) ? zt_own : zt_net;
        if (!zt_valid_id(net)) {
            zt.join_after_edit = true;
            open_editor(2);
            set.buf.clear();
        } else {
            if (net != zt_net) { zt_net = net; save_cfg(); }
            zt_action();
        }
        break;
    }
    case 6:
        if (zt_host) open_member_screen();
        else zt_create_own();
        break;
    case 7:
        refresh_zt_async();
        show_toast(L("ZeroTier 状态已刷新", "ZeroTier status refreshed"));
        break;
    case 8: {
        if (nick.empty()) nick = "darkos3562";
        bool ok = save_cfg();
        save_ra_all();
        screen = 0;
        show_toast(ok ? L("已保存", "Saved") : L("保存失败", "Save failed"));
        break;
    }
    case 9:
        zt.join_after_edit = true;
        open_editor(2);
        set.buf.clear();
        break;
    }
}

void App::editor_move(int dx, int dy) {
    if (set.target == 2) {
        set.row = (set.row + dy + 3) % 3;
        int len = set.row < 2 ? 8 : 4;
        set.col = (set.col + dx + len) % len;
        return;
    }
    int len = set.row < 5 ? 13 : 6;
    set.col = (set.col + dx + len) % len;
    set.row = (set.row + dy + 6) % 6;
    set.col = std::min(set.col, set.row < 5 ? 12 : 5);
}

void App::editor_finish() {
    if (set.target == 0) { if (!set.buf.empty()) nick = set.buf; }
    else if (set.target == 1) custom_mitm = set.buf;
    else if (set.target == 2) {
        std::string id;
        for (char c : set.buf)
            if (isxdigit((unsigned char)c)) id += (char)tolower((unsigned char)c);
        if (id.size() != 16) {
            show_toast(L("网络 ID 需 16 位十六进制", "Network ID must be 16 hex digits"));
            return;
        }
        set.buf = id;
        zt_net = id;
        if (zt.join_after_edit) {
            zt.join_after_edit = false;
            zt_join_current(false);
        }
    }
    else if (set.target == 3) {
        join_pass = set.buf;
        screen = 0;
        if (has_pending) {
            Room rm = pending_room;
            has_pending = false;
            join_room(rm, join_pass);
        }
        return;
    }
    else {
        host.pass = set.buf;
        host.pass_on = !host.pass.empty();
        screen = 3;
        return;
    }
    screen = 1;
}

void App::editor_cancel() {
    zt.join_after_edit = false;
    if (set.target == 3) { has_pending = false; join_pass.clear(); screen = 0; }
    else if (set.target == 4) screen = 3;
    else screen = 1;
}

void App::editor_activate() {
    if (set.target == 2) {
        if (set.row < 2) {
            if (set.buf.size() < 16)
                set.buf += NETID_ROWS[set.row][set.col];
            return;
        }
        switch (set.col) {
        case 0: if (!set.buf.empty()) set.buf.pop_back(); break;
        case 1: set.buf.clear(); break;
        case 2: editor_finish(); break;
        case 3: editor_cancel(); break;
        }
        return;
    }
    if (set.row < 5) {
        size_t maxlen = set.target == 0 ? 24 : (set.target == 1 ? 64 : (set.target == 2 ? 16 : 64));
        if (set.buf.size() < maxlen) set.buf += OSK_ROWS[set.row][set.col];
        return;
    }
    switch (set.col) {
    case 0: if (set.buf.size() < 64) set.buf += '|'; break;
    case 1: if (set.buf.size() < 64) set.buf += ' '; break;
    case 2: if (!set.buf.empty()) set.buf.pop_back(); break;
    case 3: set.buf.clear(); break;
    case 4: editor_finish(); break;
    case 5: editor_cancel(); break;
    }
}

// ---------- hosting (driven by EmulationStation's es_systems.cfg) ----------
