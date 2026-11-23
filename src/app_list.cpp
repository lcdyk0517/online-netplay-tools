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
    if (p == 1) {
        if (async.busy()) lan_scan_pending = true;
        else scan_lan();
    } else {
        apply_filter();
    }
}

void App::scan_lan() {
    if (async.busy()) return;
    status = L("正在扫描局域网…", "Scanning LAN…");
    auto found = std::make_shared<std::vector<Room> >();
    async.start([this, found] { lan_discover(*found, 1200, &async.cancel_flag()); }, [this, found] {
        rooms.erase(std::remove_if(rooms.begin(), rooms.end(),
            [](const Room &r) { return r.lan; }), rooms.end());
        for (auto &r : *found) {
            bool dup = false;
            for (auto &x : rooms)
                if (x.address == r.address && x.port == r.port) { dup = true; break; }
            if (!dup) rooms.push_back(r);
        }
        status = zh ? ("已连接 · " + std::to_string(rooms.size()) + " 个房间")
                    : ("Connected · " + std::to_string(rooms.size()) + " rooms");
        apply_filter();
        show_toast(std::to_string(found->size()) + L(" 个局域网主机", " LAN hosts"));
    });
}


struct LobbyRefreshData {
    bool ok = false;
    bool want_lan = false;
    std::vector<Room> rooms;
    std::vector<Room> lan;
    std::string err;
};

void App::refresh() {
    if (async.busy()) return;
    loading = true;
    status = L("正在连接 lobby…", "Contacting lobby…");
    auto d = std::make_shared<LobbyRefreshData>();
    d->want_lan = (filter == 1);
    async.start([this, d] {
        std::string body;
        d->ok = http_get(LOBBY_URL, body, d->err, &async.cancel_flag());
        if (!d->ok) return;
        d->rooms = parse_lobby(body);
        if (d->want_lan && !async.cancelled())
            lan_discover(d->lan, 1200, &async.cancel_flag());
    }, [this, d] {
        loading = false;
        loaded = true;
        if (d->ok) {
            rooms = d->rooms;
            status = zh ? ("已连接 · " + std::to_string(rooms.size()) + " 个房间")
                        : ("Connected · " + std::to_string(rooms.size()) + " rooms");
        } else {
            rooms.clear();
            bool to = d->err.find("Timeout") != std::string::npos ||
                      d->err.find("timed out") != std::string::npos;
            status = std::string(L("连接失败: ", "Failed: ")) + d->err;
            if (to && proxy.empty()) status += L(" · 需代理?", " · proxy?");
        }
        last_refresh = SDL_GetTicks();
        if (filter == 1 && !d->want_lan) {
            scan_lan();
        } else if (filter == 1) {
            rooms.erase(std::remove_if(rooms.begin(), rooms.end(),
                [](const Room &r) { return r.lan; }), rooms.end());
            for (auto &r : d->lan) {
                bool dup = false;
                for (auto &x : rooms)
                    if (x.address == r.address && x.port == r.port) { dup = true; break; }
                if (!dup) rooms.push_back(r);
            }
            status = zh ? ("已连接 · " + std::to_string(rooms.size()) + " 个房间")
                        : ("Connected · " + std::to_string(rooms.size()) + " rooms");
        }
        apply_filter();
    });
}

void App::show_toast(const std::string &s) { toast = s; toast_until = SDL_GetTicks() + 1800; }
