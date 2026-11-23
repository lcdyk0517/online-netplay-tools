#include "app.h"

#include <algorithm>
#include <string>

#include "config.h"
#include "i18n.h"
#include "theme.h"
#include "util.h"

void App::apply_filter() {
    filtered.clear();
    for (int i = 0; i < (int)rooms.size(); i++) {
        if (filter == 1 && !rooms[i].lan) continue;
        filtered.push_back(i);
    }
    if (sel >= (int)filtered.size()) sel = std::max(0, (int)filtered.size() - 1);
    scroll = std::max(0, sel - (rows - 1));
}

void App::set_page(int p) {
    p = (p % 2 + 2) % 2;
    filter = p;
    sel = 0; scroll = 0;
    if (p == 1) scan_lan();
    else apply_filter();
}

void App::scan_lan() {
    rooms.erase(std::remove_if(rooms.begin(), rooms.end(),
        [](const Room &r) { return r.lan; }), rooms.end());
    status = L("正在扫描局域网…", "Scanning LAN…");
    draw();
    std::vector<Room> found;
    lan_discover(found);
    for (auto &r : found) {
        bool dup = false;
        for (auto &x : rooms)
            if (x.address == r.address && x.port == r.port) { dup = true; break; }
        if (!dup) rooms.push_back(r);
    }
    status = zh ? ("已连接 · " + std::to_string(rooms.size()) + " 个房间")
                : ("Connected · " + std::to_string(rooms.size()) + " rooms");
    apply_filter();
    show_toast(std::to_string(found.size()) + L(" 个局域网主机", " LAN hosts"));
}

void App::refresh() {
    loading = true; loaded = false;
    status = L("正在连接 lobby…", "Contacting lobby…");
    draw();
    std::string body, err;
    loaded = true; loading = false;
    if (http_get(LOBBY_URL, body, err)) {
        rooms = parse_lobby(body);
        status = zh ? ("已连接 · " + std::to_string(rooms.size()) + " 个房间")
                    : ("Connected · " + std::to_string(rooms.size()) + " rooms");
    } else {
        rooms.clear();
        bool to = err.find("Timeout") != std::string::npos || err.find("timed out") != std::string::npos;
        status = std::string(L("连接失败: ", "Failed: ")) + err;
        if (to && proxy.empty()) status += L(" · 需代理?", " · proxy?");
    }
    last_refresh = SDL_GetTicks();
    apply_filter();
    if (filter == 1) scan_lan();
}

void App::show_toast(const std::string &s) { toast = s; toast_until = SDL_GetTicks() + 1800; }
