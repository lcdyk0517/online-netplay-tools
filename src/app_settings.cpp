#include "app.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <string>
#include <map>
#include <memory>
#include <thread>

#include "config.h"
#include "i18n.h"
#include "theme.h"
#include "util.h"

void App::open_settings() {
    screen = 1;
    set.sel = 0;
    zt.st = ZtStatus();
    zt.members.clear();
    /* Entering settings on the host is the natural point to make sure a code
     * exists; republishing an unexpired network just extends the same code.
     * Start it before the ZeroTier refresh so it is not queued behind it. */
    const long long now = (long long)time(nullptr);
    if (zt_host && zt_valid_id(zt_own) && !short_url.empty() &&
        (zt_short.empty() || zt_short_exp <= 0 || now + 60 >= zt_short_exp))
        short_publish_own(true);
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
    default: return custom_mitm.empty() ? L("自定义", "Custom") : custom_mitm;
    }
}

int App::relay_choice_count() const {
    return 5 + (int)relays.size() + 2;
}

std::string App::relay_choice_label(int i) const {
    static const char *presets[4] = { "nyc", "madrid", "saopaulo", "singapore" };
    auto with_ms = [this](std::string s, const std::string &key) {
        auto it = relay_lat.find(key);
        if (it != relay_lat.end())
            s += it->second < 0 ? L(" · 超时", " · timeout") : " · " + std::to_string(it->second) + " ms";
        return s;
    };
    if (i == 0) return L("直连", "Direct");
    if (i <= 4) return with_ms(relay_label(i), presets[i - 1]);
    const int r = (int)relays.size();
    if (i < 5 + r) return with_ms(relays[i - 5], relays[i - 5]);
    if (i == 5 + r) return L("添加中继…", "Add relay…");
    return relay_lat_pending ? L("测试延迟中…", "Testing latency…") : L("测试延迟", "Test latency");
}

bool App::relay_choice_current(int i) const {
    if (i == 0) return !use_mitm;
    static const char *presets[4] = { "nyc", "madrid", "saopaulo", "singapore" };
    if (i <= 4) return use_mitm && mitm_server == presets[i - 1];
    const int r = (int)relays.size();
    if (i < 5 + r) return use_mitm && mitm_server == "custom" && custom_mitm == relays[i - 5];
    return false;
}

void App::relay_test_latency() {
    if (lat_async.busy()) {
        show_toast(L("请稍候", "Please wait"));
        return;
    }
    struct Job {
        std::string key;      // "nyc" for a built-in, the address for a saved relay
        std::string handle;   // built-in handle, empty for a saved relay
        std::string addr;     // host|port for a saved relay
        int ms = -1;
    };
    auto jobs = std::make_shared<std::vector<Job>>();
    static const char *presets[4] = { "nyc", "madrid", "saopaulo", "singapore" };
    for (const char *p : presets)
        jobs->push_back(Job{ p, p, "", -1 });
    for (const auto &r : relays)
        jobs->push_back(Job{ r, "", r, -1 });
    auto out = std::make_shared<std::map<std::string, int>>();
    relay_lat_pending = true;
    lat_async.start([this, jobs, out] {
        std::vector<std::thread> th;
        for (auto &job : *jobs) {
            th.emplace_back([&job] {
                std::string addr = job.addr;
                if (!job.handle.empty()) {
                    std::string a; int p = 0;
                    if (!lobby_tunnel(job.handle, a, p)) return;
                    addr = a + "|" + std::to_string(p);
                }
                job.ms = tcp_latency_ms(addr, 1200);
            });
        }
        for (auto &t : th) t.join();
    }, [this, jobs, out] {
        for (const auto &job : *jobs) (*out)[job.key] = job.ms;
        relay_lat = *out;
        relay_lat_pending = false;
        show_toast(L("延迟测试完成", "Latency test done"));
    });
}

void App::open_choice(int ret_screen) {
    choice.ret = ret_screen;
    choice.sel = relay_idx();
    choice.scroll = 0;
    screen = 6;
}

void App::choice_move(int d) {
    const int n = relay_choice_count();
    choice.sel = std::max(0, std::min(n - 1, choice.sel + d));
    int vis = std::max(3, (SH - 220) / 44);
    if (choice.sel < choice.scroll) choice.scroll = choice.sel;
    if (choice.sel >= choice.scroll + vis) choice.scroll = choice.sel - vis + 1;
}

void App::choice_choose() {
    const int r = (int)relays.size();
    if (choice.sel <= 4) {
        set_relay_idx(choice.sel);
        save_cfg();
        save_ra_all();
        screen = choice.ret;
        return;
    }
    if (choice.sel < 5 + r) {
        custom_mitm = relays[choice.sel - 5];
        use_mitm = true;
        mitm_server = "custom";
        save_cfg();
        save_ra_all();
        screen = choice.ret;
        return;
    }
    if (choice.sel == 5 + r) {
        open_editor(1);
        return;
    }
    if (choice.sel == 5 + r + 1) {
        relay_test_latency();
        return;
    }
}

std::vector<int> App::settings_rows() const {
    std::vector<int> v{ 0, 1 };
    if (relay_idx() == 5) v.push_back(2);
    v.push_back(3);
    v.push_back(4);
    v.push_back(10);  /* short-code service URL */
    if (zt_host && zt_valid_id(zt_own) && !short_url.empty()) v.push_back(11);
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
    set.buf = target == 0 ? nick
            : target == 1 ? custom_mitm
            : target == 2 ? zt_net
            : target == 3 ? join_pass
            : target == 4 ? host.pass
                          : short_url;
    set.row = 0; set.col = 0; screen = 2;
}

void App::settings_change() {
    switch (set.sel) {
    case 0: open_editor(0); break;
    case 1: open_choice(1); break;
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
        if (nick.empty()) nick = "lcdyk0517";
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
    case 10:
        open_editor(5);
        break;
    case 11:
        short_publish_own();
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
    if (set.target == 0) {
        if (!set.buf.empty()) nick = set.buf;
        save_cfg();
        save_ra_all();
    }
    else if (set.target == 1) {
        std::string v = trim_str(set.buf);
        if (v.find('|') == std::string::npos) {
            size_t colon = v.rfind(':');
            if (colon != std::string::npos && colon > 0 && v.find(':') == colon)
                v = v.substr(0, colon) + "|" + v.substr(colon + 1);
        }
        size_t bar = v.find('|');
        std::string host = bar == std::string::npos ? "" : trim_str(v.substr(0, bar));
        std::string ps = bar == std::string::npos ? "" : trim_str(v.substr(bar + 1));
        char *end = nullptr;
        long port = ps.empty() ? 0 : strtol(ps.c_str(), &end, 10);
        if (host.empty() || ps.empty() || !end || *end || port <= 0 || port > 65535) {
            show_toast(L("格式应为 host|port", "Use host|port"));
            return;
        }
        custom_mitm = host + "|" + std::to_string(port);
        use_mitm = true;
        mitm_server = "custom";
        relay_remember(custom_mitm);
        save_ra_all();
        show_toast(L("中继已保存", "Relay saved"));
    }
    else if (set.target == 2) {
        std::string id;
        for (char c : set.buf)
            if (isxdigit((unsigned char)c)) id += (char)tolower((unsigned char)c);
        if (id.size() >= 3 && id.size() <= 6) {
            /* Short lookup code: resolve to the 16-hex id, then join. */
            set.buf = id;
            screen = 1;
            zt_join_by_code(id);
            return;
        }
        if (id.size() != 16) {
            show_toast(L("网络 ID 16 位或短码 3-6 位", "Need a 16-hex ID or a 3-6 char code"));
            return;
        }
        set.buf = id;
        zt_net = id;
        if (zt.join_after_edit) {
            zt.join_after_edit = false;
            zt_join_current(false);
        }
    }
    else if (set.target == 5) {
        std::string u = trim_str(set.buf);
        while (!u.empty() && u.back() == '/') u.pop_back();
        short_url = u;
        save_cfg();
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
    else if (set.target == 4) {
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
        size_t maxlen = set.target == 0 ? 24
                       : set.target == 2 ? 16
                       : set.target == 5 ? 96
                                         : 64;
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
