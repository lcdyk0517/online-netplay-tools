#include "app.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "config.h"
#include "i18n.h"
#include "theme.h"

void App::draw() {
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    const int M = 40;
    const int sepY = SH - 56, footY = SH - 38;

    // header
    round({M, 26, 44, 44}, 12, ACCENT);
    fill({M + 12, 38, 20, 20}, CARD);
    text(f28, L("在线房间", "Online Rooms"), M + 60, 26, TXT);
    const char *chips[2] = { L("全部", "All"), L("局域网", "LAN") };
    int cx = M + 320;
    for (int i = 0; i < 2; i++) {
        int w = 0; TTF_SizeUTF8(f18, chips[i], &w, nullptr); w += 36;
        SDL_Rect rc{cx, 32, w, 38};
        round(rc, 19, i == filter ? ACCENT : CHIP);
        text(f18, chips[i], cx + 18, 40, i == filter ? CARD : MUTED);
        cx += w + 12;
    }
    std::string st = status;
    if (loaded && rooms.size() && SDL_GetTicks() - last_refresh < 30000)
        st += " · " + std::to_string(30 - (SDL_GetTicks() - last_refresh) / 1000) + "s";
    text(f16, st, M, 80, MUTED, SW - 2 * M);

    // list + detail panel geometry follows the actual panel size
    const int y0 = 104, rowH = 74;
    int panelW = std::min(380, std::max(300, SW * 28 / 100));
    int panelX = SW - M - panelW;
    int listW = panelX - M - 20;
    rows = std::max(3, (sepY - y0) / rowH);
    if (!loaded && loading) {
        text(f22, L("加载中…", "Loading…"), M + 20, SH / 2 - 20, MUTED);
    } else if (filtered.empty()) {
        text(f22, L("没有符合条件的房间", "No rooms match the filter"), M + 20, SH / 2 - 20, MUTED);
    }
    for (int row = 0; row < rows; row++) {
        int idx = scroll + row;
        if (idx >= (int)filtered.size()) break;
        const Room &rm = rooms[filtered[idx]];
        SDL_Rect card{M, y0 + row * rowH, listW, 64};
        round(card, 10, idx == sel ? SEL_BG : CARD);
        if (idx == sel) fill({card.x, card.y + 6, 4, card.h - 12}, ACCENT);
        int tx = card.x + 20;
        // right block: line 1 = players/mode/lock, line 2 = local state
        std::string r1 = std::to_string(rm.players) + (zh ? " 人" : " pl") +
                         (rm.lan ? " · LAN" : (is_mitm(rm) ? " · MITM" : (zh ? " · 直连" : " · Direct")));
        if (rm.has_password) r1 += zh ? " · 锁" : " · LOCK";
        std::string c32; std::string core = find_core(rm.corename, cores, c32);
        LocalGame lg = find_game(rm.gamename, rm.gamecrc, names, crcs);
        std::string r2 = !rm.connectable ? L("不可加入", "Not joinable")
            : (std::string(lg.found ? (zh ? "游戏 √" : "Game √") : (zh ? "游戏 ×" : "Game ×")) + "  " +
               (core.empty() ? (zh ? "核心 ×" : "Core ×") : (zh ? "核心 √" : "Core √")));
        SDL_Color r2c = !rm.connectable ? BAD : (lg.found && !core.empty() ? ACCENT_D : WARN);
        int r1w = 0, r2w = 0;
        TTF_SizeUTF8(f16, r1.c_str(), &r1w, nullptr);
        TTF_SizeUTF8(f16, r2.c_str(), &r2w, nullptr);
        int rw = std::max(r1w, r2w);
        text(f16, r1, card.x + card.w - 14 - r1w, card.y + 8, MUTED);
        text(f16, r2, card.x + card.w - 14 - r2w, card.y + 36, r2c);
        int tw = card.w - 20 - rw - 26;
        text(f22, rm.gamename, tx, card.y + 32, TXT, tw);
        std::string meta = rm.corename;
        if (!rm.coreversion.empty()) meta += " " + rm.coreversion;
        meta += " · " + rm.nickname;
        if (!rm.country.empty()) meta += " · " + rm.country;
        text(f16, meta, tx, card.y + 8, MUTED, tw + 20);
    }

    // detail panel
    SDL_Rect panel{panelX, y0, panelW, sepY - y0 - 4};
    round(panel, 16, CARD);
    if (!filtered.empty()) {
        const Room &rm = rooms[filtered[sel]];
        int px = panel.x + 22, py = panel.y + 20, pw = panelW - 44;
        text(f22, rm.gamename, px, py, TXT, pw); py += 34;
        std::string coreline = rm.corename;
        if (!rm.coreversion.empty()) coreline += " " + rm.coreversion;
        int cw = 0; TTF_SizeUTF8(f16, coreline.c_str(), &cw, nullptr);
        SDL_Rect cb{px, py, std::min(cw + 20, pw), 26};
        round(cb, 13, CHIP);
        text(f16, coreline, px + 10, py + 4, ACCENT_D, pw - 20);
        py += 42;
        text(f16, std::string(L("房主  ", "Host  ")) + rm.nickname + (rm.country.empty() ? "" : " · " + rm.country), px, py, MUTED, pw); py += 26;
        std::string addr = (is_mitm(rm) && !rm.mitm_ip.empty())
            ? rm.mitm_ip + ":" + std::to_string(rm.mitm_port)
            : rm.address + ":" + std::to_string(rm.port);
        text(f16, std::string(L("地址  ", "Addr  ")) + addr, px, py, MUTED, pw); py += 26;
        std::string mode = rm.lan ? "LAN" : (is_mitm(rm) ? "MITM" : L("直连", "Direct"));
        if (is_mitm(rm) && !rm.mitm_session.empty()) mode += " · " + rm.mitm_session.substr(0, 8);
        text(f16, std::string(L("方式  ", "Mode  ")) + mode, px, py, MUTED, pw); py += 26;
        text(f16, std::string(L("人数  ", "Players  ")) + std::to_string(rm.players) +
             (rm.spectators ? " +" + std::to_string(rm.spectators) : ""), px, py, MUTED); py += 34;
        fill({px, py, pw, 1}, LINE); py += 16;
        text(f18, L("本地状态", "Local status"), px, py, TXT); py += 30;
        std::string c32; std::string core = find_core(rm.corename, cores, c32);
        LocalGame lg = find_game(rm.gamename, rm.gamecrc, names, crcs);
        text(f16, std::string(L("游戏  ", "Game  ")) + (lg.found ? (lg.by_crc ? L("√ 校验匹配", "√ CRC match") : L("√ 已找到", "√ Found")) : L("× 本地没有", "× Missing")), px, py, lg.found ? ACCENT_D : BAD, pw); py += 26;
        text(f16, std::string(L("核心  ", "Core  ")) + (core.empty() ? L("× 未安装", "× Missing") : L("√ 已安装", "√ Installed")), px, py, core.empty() ? BAD : ACCENT_D, pw); py += 26;
        text(f16, std::string(L("版本  ", "Version  ")) + rm.coreversion, px, py, MUTED, pw); py += 26;
        if (rm.has_password) { text(f16, L("! 需要密码（按 A 输入）", "! Password (A to enter)"), px, py, WARN, pw); py += 26; }
        if (!rm.connectable) { text(f16, L("! 主机不可连接", "! Not connectable"), px, py, WARN, pw); py += 26; }
        bool ok = rm.connectable && !core.empty() && lg.found;
        SDL_Rect jb{px, panel.y + panel.h - 58, pw, 46};
        round(jb, 12, ok ? ACCENT : CHIP);
        std::string jt = L("加入房间 (A)", "Join (A)");
        int jw = 0; TTF_SizeUTF8(f18, jt.c_str(), &jw, nullptr);
        text(f18, jt, jb.x + (jb.w - jw) / 2, jb.y + 11, ok ? CARD : MUTED);
        if (!ok) {
            std::string why = !rm.connectable ? L("房间不可连接", "Not connectable")
                            : core.empty() ? L("缺少模拟核心", "Core missing")
                                           : L("缺少本地游戏", "Game missing");
            int ww = 0; TTF_SizeUTF8(f16, why.c_str(), &ww, nullptr);
            text(f16, why, jb.x + (jb.w - ww) / 2, jb.y - 24, WARN);
        }
    }

    // bottom hints
    fill({M, sepY, SW - 2 * M, 1}, LINE);
    const char *hints[6][2] = {
        { "A", L("加入", "Join") }, { "Y", L("刷新", "Refresh") },
        { "L/R", L("页面", "Page") }, { "X", L("开房", "Host") },
        { "Start", L("设置", "Settings") },
        { "B", L("退出 (手柄 Select+Start)", "Exit (Select+Start)") } };
    int hx = M;
    for (auto &h : hints) {
        int bw = 0, lw2 = 0;
        TTF_SizeUTF8(f16, h[0], &bw, nullptr);
        TTF_SizeUTF8(f16, h[1], &lw2, nullptr);
        int chipw = std::max(24, bw + 14);
        SDL_Rect b{hx, footY, chipw, 24};
        round(b, 12, CHIP);
        text(f16, h[0], b.x + (chipw - bw) / 2, b.y + 3, ACCENT_D);
        text(f16, h[1], b.x + chipw + 8, b.y + 3, MUTED);
        hx += chipw + 8 + lw2 + 18;
    }
    std::string pinfo = pad_name.empty() ? L("手柄未识别", "No pad") : pad_name;
    int pw2 = 0; TTF_SizeUTF8(f16, pinfo.c_str(), &pw2, nullptr);
    text(f16, pinfo, SW - M - pw2, footY + 4, pad_name.empty() ? BAD : MUTED);
    if (!toast.empty() && SDL_GetTicks() < toast_until) {
        int tw = 0; TTF_SizeUTF8(f18, toast.c_str(), &tw, nullptr);
        SDL_Rect tb{SW / 2 - tw / 2 - 20, SH - 110, tw + 40, 36};
        round(tb, 18, TXT);
        text(f18, toast, tb.x + 20, tb.y + 6, CARD);
    }
    if (screen == 1) draw_settings();
    else if (screen == 2) draw_editor();
    else if (screen == 3) draw_host();
    else if (screen == 4) draw_picker();
    else if (screen == 5) draw_members();
    if (!busy.empty()) {
        fill({0, 0, SW, SH}, DIM);
        int tw = 0;
        TTF_SizeUTF8(f22, busy.c_str(), &tw, nullptr);
        SDL_Rect box{SW / 2 - tw / 2 - 30, SH / 2 - 30, tw + 60, 60};
        round(box, 14, CARD);
        text(f22, busy, box.x + 30, box.y + 16, TXT);
    }
    SDL_RenderPresent(r);
}


void App::draw_settings() {
    fill({0, 0, SW, SH}, DIM);
    int w = std::min(SW - 100, 760), h = std::min(SH - 80, 600);
    int x = (SW - w) / 2, y = (SH - h) / 2;
    round({x, y, w, h}, 18, CARD);
    text(f28, L("设置", "Settings"), x + 28, y + 20, TXT);
    int ry = y + 74, rh = 46;
    for (int id : settings_rows()) {
        SDL_Rect row{x + 20, ry, w - 40, rh - 8};
        if (id == ssel) {
            round(row, 12, SEL_BG);
            fill({row.x, row.y + 8, 4, row.h - 16}, ACCENT);
        }
        std::string label, value;
        if (id == 0) { label = L("用户名", "Nickname"); value = nick; }
        else if (id == 1) { label = L("中继服务器", "Relay server"); value = relay_label(relay_idx()); }
        else if (id == 2) { label = L("自定义中继", "Custom relay"); value = custom_mitm.empty() ? L("未设置", "Not set") : custom_mitm; }
        else if (id == 3) { label = L("公开房间", "Public announce"); value = announce ? L("开", "On") : L("关", "Off"); }
        else if (id == 4) { label = zt_host ? L("本机网络 ID", "Hosted network ID")
                                            : L("当前网络 ID", "Current network ID");
                            value = (zt_host ? zt_own : zt_net).empty()
                                  ? L("未设置", "Not set") : (zt_host ? zt_own : zt_net); }
        else if (id == 5) {
            label = L("ZeroTier", "ZeroTier");
            value = zt_state_text();
            if (zt_st.state == "notjo")
                value += zt_valid_id(zt_net) ? L(" · 按 A 加入网络", " · A to join")
                                             : L(" · 按 A 输入网络 ID", " · A to enter network ID");
        }
        else if (id == 6) { label = zt_host ? L("成员管理", "Members")
                                            : L("创建本机网络", "Create local network");
                            value = zt_host ? std::to_string(zt_pending()) + L(" 台待授权", " pending") : ""; }
        else if (id == 7) { label = L("刷新 ZeroTier", "Refresh ZeroTier"); }
        else if (id == 9) { label = L("加入 ZeroTier 网络", "Join a ZeroTier network"); }
        else { label = L("保存并返回", "Save and back"); }
        text(f22, label, row.x + 18, row.y + 12, TXT);
        if (!value.empty()) text(f18, value, row.x + 250, row.y + 14, ACCENT_D, row.w - 280);
        ry += rh;
    }
    if (!zt_st.node.empty())
        text(f16, L("节点 ID: ", "Node ID: ") + zt_st.node, x + 28, y + h - 58, MUTED, w - 56);
    text(f16, L("A 修改/加入 · B 返回 · Start 保存", "A edit/join · B back · Start save"),
         x + 28, y + h - 34, MUTED);
}


void App::draw_editor() {
    fill({0, 0, SW, SH}, DIM);
    int w = std::min(SW - 60, 900), h = std::min(SH - 60, 500);
    int x = (SW - w) / 2, y = (SH - h) / 2;
    round({x, y, w, h}, 18, CARD);
    const char *etitle = edit_target == 0 ? L("用户名", "Nickname")
                       : edit_target == 1 ? L("自定义中继 (host|port)", "Custom relay (host|port)")
                       : edit_target == 2 ? L("ZeroTier 网络 ID", "ZeroTier network ID")
                                          : L("房间密码", "Room password");
    text(f28, etitle, x + 24, y + 18, TXT);
    int bx = x + 24, by = y + 66, bw = w - 48;
    round({bx, by, bw, 46}, 10, CHIP);
    std::string shown = (edit_target == 3 || edit_target == 4) ? std::string(edit_buf.size(), '*') : edit_buf;
    text(f22, shown + "_", bx + 14, by + 8, TXT, bw - 28);
    int cols = 13, kw = (bw - (cols - 1) * 4) / cols, kh = 40, gy = by + 62;
    for (int row = 0; row < 5; row++) {
        int len = (int)strlen(OSK_ROWS[row]);
        for (int col = 0; col < len; col++) {
            SDL_Rect k{bx + col * (kw + 4), gy + row * (kh + 4), kw, kh};
            bool on = erow == row && ecol == col;
            round(k, 8, on ? ACCENT : CHIP);
            std::string s(1, OSK_ROWS[row][col]);
            int tw = 0; TTF_SizeUTF8(f18, s.c_str(), &tw, nullptr);
            text(f18, s, k.x + (kw - tw) / 2, k.y + 8, on ? CARD : TXT);
        }
    }
    const char *labels[6] = { "|", L("空格", "Space"), L("删除", "Del"), L("清空", "Clear"),
                              L("确定", "OK"), L("取消", "Cancel") };
    int spw = (bw - 5 * 6) / 6, sy = gy + 5 * (kh + 4) + 6;
    for (int i = 0; i < 6; i++) {
        SDL_Rect k{bx + i * (spw + 6), sy, spw, 42};
        bool on = erow == 5 && ecol == i;
        round(k, 8, on ? ACCENT : CHIP);
        int tw = 0; TTF_SizeUTF8(f18, labels[i], &tw, nullptr);
        text(f18, labels[i], k.x + (spw - tw) / 2, k.y + 9, on ? CARD : TXT);
    }
    text(f16, L("A 输入 · B 删除 · Y 完成 · X 取消", "A type · B del · Y done · X cancel"),
         x + 24, y + h - 34, MUTED);
}



void App::draw_members() {
    fill({0, 0, SW, SH}, DIM);
    int n = (int)zt_members_list.size();
    int vis = std::max(3, (SH - 220) / 44);
    int w = std::min(SW - 100, 860), h = std::min(SH - 100, vis * 44 + 150);
    int x = (SW - w) / 2, y = (SH - h) / 2;
    round({x, y, w, h}, 18, CARD);
    text(f28, L("成员管理", "Members"), x + 24, y + 18, TXT);
    text(f16, zt_net, x + w - 24 - 200, y + 28, MUTED, 200);
    int ly = y + 68;
    for (int i = 0; i < vis; i++) {
        int idx = zt_mscroll + i;
        if (idx >= n) break;
        const ZtMember &m = zt_members_list[idx];
        SDL_Rect row{x + 16, ly + i * 44, w - 32, 40};
        if (idx == zt_msel) {
            round(row, 8, SEL_BG);
            fill({row.x, row.y + 6, 4, row.h - 12}, ACCENT);
        }
        text(f18, m.id, row.x + 14, row.y + 8, TXT, row.w - 200);
        std::string sub = m.authorized ? L("已允许", "Allowed") + (m.ip.empty() ? "" : " " + m.ip)
                                       : L("待授权", "Pending");
        int sw = 0; TTF_SizeUTF8(f16, sub.c_str(), &sw, nullptr);
        text(f16, sub, row.x + row.w - 14 - sw, row.y + 11, m.authorized ? ACCENT_D : WARN);
    }
    if (n == 0) text(f22, L("还没有设备请求加入", "No device has joined yet"), x + 24, y + 90, MUTED);
    text(f16, L("A 允许/拒绝 · X 删除 · Start 清空 · Y 刷新 · B 返回",
                "A allow/deny · X remove · Start clear · Y refresh · B back"),
         x + 24, y + h - 34, MUTED);
}


void App::draw_host() {
    fill({0, 0, SW, SH}, DIM);
    int w = std::min(SW - 100, 760), h = std::min(SH - 80, 560);
    int x = (SW - w) / 2, y = (SH - h) / 2;
    round({x, y, w, h}, 18, CARD);
    text(f28, L("开房间", "Host a room"), x + 28, y + 18, TXT);
    int ry = y + 74, rh = 46;
    const char *labels[8] = {
        L("系统", "System"), L("游戏", "Game"), L("核心", "Core"),
        L("中继", "Relay"), L("公开房间", "Public announce"),
        L("仅局域网", "LAN only"), L("房间密码", "Room password"),
        L("开始开房", "Start hosting") };
    for (int id = 0; id < 8; id++) {
        SDL_Rect row{x + 20, ry, w - 40, rh - 8};
        if (id == hsel) {
            round(row, 12, SEL_BG);
            fill({row.x, row.y + 8, 4, row.h - 16}, ACCENT);
        }
        std::string value;
        switch (id) {
        case 0: value = hsys < 0 ? L("未选择", "Not selected") : system_alias(); break;
        case 1: value = hgame_path.empty() ? L("未选择", "Not selected") : hgame_name; break;
        case 2: value = hcore.empty() ? L("未选择", "Not selected") : hcore; break;
        case 3: value = lan_only ? L("局域网直连", "LAN direct") : relay_label(relay_idx()); break;
        case 4: value = lan_only ? L("关 (仅局域网)", "Off (LAN only)")
                                 : (announce ? L("开", "On") : L("关", "Off")); break;
        case 5: value = lan_only ? L("开", "On") : L("关", "Off"); break;
        case 6: value = host_pass_on ? L("开", "On") : L("关", "Off"); break;
        default: break;
        }
        text(f22, labels[id], row.x + 18, row.y + 10, TXT);
        if (!value.empty()) text(f18, value, row.x + 190, row.y + 13, ACCENT_D, row.w - 210);
        ry += rh;
    }
    text(f16, L("用户名: ", "Nickname: ") + nick + L("  ·  核心按 es_systems 自动选择", "  ·  cores from es_systems"),
         x + 28, y + h - 60, MUTED, w - 56);
    text(f16, L("A 选择/修改 · B 返回 · Start 开始", "A select · B back · Start host"),
         x + 28, y + h - 34, MUTED);
}


void App::draw_picker() {
    fill({0, 0, SW, SH}, DIM);
    int n = picker_count();
    int vis = std::max(3, (SH - 220) / 44);
    int w = std::min(SW - 100, 860), h = std::min(SH - 100, vis * 44 + 150);
    int x = (SW - w) / 2, y = (SH - h) / 2;
    round({x, y, w, h}, 18, CARD);
    const char *title = pick_kind == 0 ? L("选择系统", "Choose system")
                      : pick_kind == 1 ? L("选择游戏", "Choose game")
                                       : L("选择核心", "Choose core");
    text(f28, title, x + 24, y + 18, TXT);
    if (pick_kind == 1 && hsys >= 0)
        text(f16, systems[hsys].fullname.empty() ? systems[hsys].name : systems[hsys].fullname,
             x + w - 24 - 200, y + 28, MUTED, 200);
    int ly = y + 68;
    for (int i = 0; i < vis; i++) {
        int idx = pick_scroll + i;
        if (idx >= n) break;
        SDL_Rect row{x + 16, ly + i * 44, w - 32, 40};
        if (idx == pick_sel) {
            round(row, 8, SEL_BG);
            fill({row.x, row.y + 6, 4, row.h - 12}, ACCENT);
        }
        std::string name, sub;
        if (pick_kind == 0) {
            const EsSystem &sy = systems[idx];
            name = sy.fullname.empty() ? sy.name : sy.fullname;
            sub = sy.name;
        } else if (pick_kind == 1) {
            name = sys_games[idx].name;
            sub = systems[hsys].name;
        } else {
            name = sys_cores[idx];
            auto it = cores.find(name);
            if (it != cores.end())
                sub = it->second.path64.empty() ? "32" : (it->second.path32.empty() ? "64" : "64+32");
        }
        int sw = 0; TTF_SizeUTF8(f16, sub.c_str(), &sw, nullptr);
        text(f18, name, row.x + 14, row.y + 8, TXT, row.w - sw - 40);
        text(f16, sub, row.x + row.w - 14 - sw, row.y + 11, MUTED);
    }
    if (n == 0)
        text(f22, pick_kind == 1 ? L("这个系统下没有游戏", "No games in this system")
                                 : L("没有可选内容", "Nothing to choose"),
             x + 24, y + 90, MUTED);
    text(f16, L("A 选择 · B 返回", "A select · B back"), x + 24, y + h - 34, MUTED);
}

