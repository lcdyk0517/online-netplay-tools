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
    ssel = 0;
    zt_adopt_own();
    zt_st = zt_status(zt_net);   // fast when the daemon is not running
    zt_refresh_members();
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
    for (; i < (int)rs.size(); i++) if (rs[i] == ssel) break;
    i = std::max(0, std::min((int)rs.size() - 1, i + d));
    ssel = rs[i];
}

void App::open_editor(int target) {
    edit_target = target;
    edit_buf = target == 0 ? nick : (target == 1 ? custom_mitm : (target == 2 ? zt_net : (target == 3 ? join_pass : host_pass)));
    erow = 0; ecol = 0; screen = 2;
}

void App::settings_change() {
    switch (ssel) {
    case 0: open_editor(0); break;
    case 1: set_relay_idx((relay_idx() + 1) % 6); break;
    case 2: open_editor(1); break;
    case 3: announce = !announce; break;
    case 4:
        if (zt_host) show_toast(L("本机网络，成员在成员管理里", "Hosted here; see Members"));
        else open_editor(2);
        break;
    case 5:
        if (!zt_valid_id(zt_net)) {
            join_after_edit = true;
            open_editor(2);
            edit_buf.clear();
        } else {
            zt_action();
        }
        break;
    case 6:
        if (zt_host) open_member_screen();
        else zt_create_own();
        break;
    case 7:
        zt_st = zt_status(zt_net);
        zt_refresh_members();
        show_toast(L("ZeroTier 状态已刷新", "ZeroTier status refreshed"));
        break;
    case 8:
        if (nick.empty()) nick = "darkos3562";
        save_cfg();
        save_ra_all();
        screen = 0;
        show_toast(L("已保存", "Saved"));
        break;
    case 9:
        join_after_edit = true;
        open_editor(2);
        edit_buf.clear();
        break;
    }
}

void App::editor_move(int dx, int dy) {
    int len = erow < 5 ? 13 : 6;
    ecol = (ecol + dx + len) % len;
    erow = (erow + dy + 6) % 6;
    ecol = std::min(ecol, erow < 5 ? 12 : 5);
}

void App::editor_finish() {
    if (edit_target == 0) { if (!edit_buf.empty()) nick = edit_buf; }
    else if (edit_target == 1) custom_mitm = edit_buf;
    else if (edit_target == 2) {
        zt_net = lower(edit_buf);
        if (join_after_edit) {
            join_after_edit = false;
            zt_join_current(false);
        }
    }
    else if (edit_target == 3) {
        join_pass = edit_buf;
        screen = 0;
        if (has_pending) {
            Room rm = pending_room;
            has_pending = false;
            join_room(rm, join_pass);
        }
        return;
    }
    else {
        host_pass = edit_buf;
        host_pass_on = !host_pass.empty();
        screen = 3;
        return;
    }
    screen = 1;
}

void App::editor_cancel() {
    join_after_edit = false;
    if (edit_target == 3) { has_pending = false; join_pass.clear(); screen = 0; }
    else if (edit_target == 4) screen = 3;
    else screen = 1;
}

void App::editor_activate() {
    if (erow < 5) {
        size_t maxlen = edit_target == 0 ? 24 : (edit_target == 1 ? 64 : (edit_target == 2 ? 16 : 64));
        if (edit_buf.size() < maxlen) edit_buf += OSK_ROWS[erow][ecol];
        return;
    }
    switch (ecol) {
    case 0: if (edit_buf.size() < 64) edit_buf += '|'; break;
    case 1: if (edit_buf.size() < 64) edit_buf += ' '; break;
    case 2: if (!edit_buf.empty()) edit_buf.pop_back(); break;
    case 3: edit_buf.clear(); break;
    case 4: editor_finish(); break;
    case 5: editor_cancel(); break;
    }
}

// ---------- hosting (driven by EmulationStation's es_systems.cfg) ----------
