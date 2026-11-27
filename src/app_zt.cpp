#include "app.h"

#include <algorithm>
#include <ctime>
#include <string>
#include <unistd.h>

#include "config.h"
#include "i18n.h"
#include "shortcode.h"
#include "theme.h"


void App::zt_refresh_members() {
    if (!zt_host || !zt_valid_id(zt_own)) { zt.members.clear(); return; }
    refresh_zt_async();
}

int App::zt_pending() const {
    int n = 0;
    for (auto &m : zt.members) if (!m.authorized) n++;
    return n;
}

std::string App::zt_target() const {
    return zt_host && !zt_own.empty() ? zt_own : zt_net;
}

struct ZtRefreshData {
    std::string target;
    std::string adopt;
    ZtStatus st;
    std::vector<ZtMember> members;
};

void App::refresh_zt_async() {
    if (async.busy()) { zt_refresh_pending = true; return; }
    auto d = std::make_shared<ZtRefreshData>();
    d->target = zt_target();
    const bool host = zt_host;
    const std::string own = zt_own;
    async.start([this, d, host, own] {
        std::string target = d->target;
        bool is_host = host;
        std::string host_net = own;
        /* Adopt a network hosted on this device if the config forgot it. */
        if (!is_host && zt_installed() && zt_daemon_up()) {
            std::vector<std::string> nets = zt_controller_networks();
            if (!nets.empty()) {
                d->adopt = nets[0];
                target = d->adopt;
                is_host = true;
                host_net = d->adopt;
            }
        }
        if (async.cancelled()) return;
        /* A host stays joined to its own network; only do this when the
         * daemon is already running so opening the tool never starts it. */
        if (is_host && zt_valid_id(host_net) && zt_daemon_up()) {
            if (!zt_tun_ok()) {
                bool tun_missing = true;
                if (zt_tun_ensure() && tun_missing) zt_restart_daemon();
            }
            zt_cli("join", host_net);
            usleep(300000);
        }
        if (async.cancelled()) return;
        d->st = zt_status(target);
        if (async.cancelled()) return;
        if (zt_valid_id(host_net))
            d->members = zt_fetch_members(host_net);
    }, [this, d] {
        bool changed = false;
        bool became_host = false;
        if (!d->adopt.empty() && !zt_host) {
            zt_own = d->adopt;
            zt_host = true;
            changed = true;
            became_host = true;
        }
        if (zt_host && zt_valid_id(zt_own) && zt_net != zt_own) {
            /* Own network is the current one on a host device. */
            zt_net = zt_own;
            changed = true;
        }
        if (changed) save_cfg();
        zt.st = d->st;
        zt.members = d->members;
        if (zt.msel >= (int)zt.members.size()) zt.msel = 0;
        if (became_host && !short_url.empty() && (zt_short.empty() || zt_short_exp <= 0))
            short_publish_own(true);
    });
}

void App::poll_async() {
    async.poll();
    lat_async.poll();
    if (lat_async.consume_failure()) {
        relay_lat_pending = false;
        show_toast(L("延迟测试失败", "Latency test failed"));
    }
    if (async.consume_failure())
        show_toast(L("后台任务失败", "Background task failed"));
    if (async.busy()) return;
    if (zt_refresh_pending) {
        zt_refresh_pending = false;
        refresh_zt_async();
    }
    if (!async.busy() && short_pub_pending) {
        short_pub_pending = false;
        short_publish_own(true);
    }
    if (!async.busy() && lan_scan_pending) {
        lan_scan_pending = false;
        scan_lan();
    }
    if (!async.busy() && lobby_refresh_pending) {
        lobby_refresh_pending = false;
        refresh();
    }
}

struct ZtCreateData {
    bool ok = false;
    int err = 0;  // 1 not installed, 2 tun, 3 daemon, 4 node id, 5 create
    std::string nw;
    std::string scode;   // short code published for this network
    long long sexp = 0;
    ZtStatus st;
    std::vector<ZtMember> members;
};

void App::zt_create_own() {
    if (async.busy()) { show_toast(L("正在处理，请稍候", "Busy, please wait")); return; }
    busy = L("正在创建 ZeroTier 网络…", "Creating ZeroTier network…");
    auto d = std::make_shared<ZtCreateData>();
    async.start([this, d] {
        if (async.cancelled()) return;
        if (!zt_installed()) { d->err = 1; return; }
        bool tun_missing = !zt_tun_ok();
        if (!zt_tun_ensure()) { d->err = 2; return; }
        if (tun_missing && zt_daemon_up()) zt_restart_daemon();
        if (!zt_ensure_daemon()) { d->err = 3; return; }
        ZtStatus st = zt_status("");
        if (st.node.size() != 10) { d->err = 4; return; }
        if (async.cancelled()) return;
        std::string nw = zt_create_network(st.node);
        if (nw.empty()) { d->err = 5; return; }
        zt_cli("join", nw);
        zt_authorize_member(nw, st.node, true);
        usleep(300000);
        d->members = zt_fetch_members(nw);
        d->st = zt_status(nw);
        d->nw = nw;
        if (!short_url.empty()) {
            std::string e;
            short_publish(nw, d->scode, e, &d->sexp);  /* best effort */
        }
        d->ok = true;
    }, [this, d] {
        busy.clear();
        if (!d->ok) {
            switch (d->err) {
            case 1: show_toast(L("未找到 zerotier-one", "zerotier-one not installed")); break;
            case 2: show_toast(L("无法加载 TUN (tun.ko)", "Cannot load TUN (tun.ko)")); break;
            case 3: show_toast(L("ZeroTier 启动失败", "ZeroTier failed to start")); break;
            case 4: show_toast(L("读取节点 ID 失败", "Cannot read node ID")); break;
            default: show_toast(L("创建网络失败", "Network creation failed")); break;
            }
            return;
        }
        zt_net = d->nw;
        zt_own = d->nw;
        zt_host = true;
        zt_short = d->scode;
        zt_short_exp = d->sexp;
        save_cfg();
        zt.st = d->st;
        zt.members = d->members;
        show_toast(L("已创建网络 ", "Network created ") + d->nw +
                   (d->scode.empty() ? "" : L(" · 短码 ", " · code ") + d->scode));
    });
}

void App::open_member_screen() {
    zt_refresh_members();
    zt.msel = 0;
    zt.mscroll = 0;
    screen = 5;
}

void App::member_move(int d) {
    int n = (int)zt.members.size();
    if (!n) return;
    zt.msel = std::max(0, std::min(n - 1, zt.msel + d));
    int vis = std::max(3, (SH - 220) / 44);
    if (zt.msel < zt.mscroll) zt.mscroll = zt.msel;
    if (zt.msel >= zt.mscroll + vis) zt.mscroll = zt.msel - vis + 1;
}

void App::member_toggle() {
    if (zt.msel >= (int)zt.members.size()) return;
    if (async.busy()) { show_toast(L("正在处理，请稍候", "Busy, please wait")); return; }
    const std::string id = zt.members[zt.msel].id;
    const bool auth = !zt.members[zt.msel].authorized;
    const std::string own = zt_own;
    auto d = std::make_shared<std::vector<ZtMember> >();
    async.start([this, id, auth, own, d] {
        if (async.cancelled()) return;
        zt_authorize_member(own, id, auth);
        *d = zt_fetch_members(own);
    }, [this, d] {
        zt.members = *d;
        if (zt.msel >= (int)zt.members.size()) zt.msel = 0;
    });
}

void App::member_delete() {
    if (zt.msel >= (int)zt.members.size()) return;
    std::string id = zt.members[zt.msel].id;
    if (zt.confirm == "del:" + id && SDL_GetTicks() < zt.confirm_until) {
        zt.confirm.clear();
        if (async.busy()) { show_toast(L("正在处理，请稍候", "Busy, please wait")); return; }
        const std::string own = zt_own;
        auto d = std::make_shared<std::vector<ZtMember> >();
        async.start([this, id, own, d] {
            if (async.cancelled()) return;
            zt_delete_member(own, id);
            *d = zt_fetch_members(own);
        }, [this, d, id] {
            zt.members = *d;
            if (zt.msel >= (int)zt.members.size()) zt.msel = 0;
            show_toast(L("已删除成员 ", "Removed member ") + id);
        });
    } else {
        zt.confirm = "del:" + id;
        zt.confirm_until = SDL_GetTicks() + 3000;
        show_toast(L("再按一次 X 删除该成员", "Press X again to remove"));
    }
}

void App::member_clear() {
    if (zt.confirm == "clear" && SDL_GetTicks() < zt.confirm_until) {
        zt.confirm.clear();
        if (async.busy()) { show_toast(L("正在处理，请稍候", "Busy, please wait")); return; }
        const std::string own = zt_own;
        std::vector<std::string> ids;
        for (auto &m : zt.members) ids.push_back(m.id);
        auto d = std::make_shared<std::vector<ZtMember> >();
        async.start([this, ids, own, d] {
            if (async.cancelled()) return;
            for (auto &id : ids) zt_delete_member(own, id);
            *d = zt_fetch_members(own);
        }, [this, d] {
            zt.members = *d;
            zt.msel = 0;
            show_toast(L("已清空所有成员", "All members removed"));
        });
    } else {
        zt.confirm = "clear";
        zt.confirm_until = SDL_GetTicks() + 3000;
        show_toast(L("再按一次 Start 清空全部成员", "Press Start again to clear all"));
    }
}

std::string App::zt_state_text() const {
    const std::string &s = zt.st.state;
    if (s == "noinst") return L("未安装", "Not installed");
    if (s == "notun") return L("内核未启用 TUN", "Kernel TUN off");
    if (s == "down") return L("未启动", "Stopped");
    if (s == "notjo") return L("未加入", "Not joined");
    if (s == "notfound") return L("网络不存在", "Network not found");
    if (s == "denied") return L("未授权", "Access denied");
    if (s == "requesting") return L("正在获取配置", "Requesting config");
    if (s == "ok") return L("已连接", "Connected") + (zt.st.ip.empty() ? "" : " " + zt.st.ip);
    return s.empty() ? L("未启动", "Stopped") : s;
}

struct ZtJoinData {
    bool ok = false;
    int err = 0;   // 1 bad id, 2 not installed, 3 tun, 4 daemon
    bool left = false;
    ZtStatus st;
};

void App::zt_join_current(bool leave_if_joined) {
    if (async.busy()) { show_toast(L("正在处理，请稍候", "Busy, please wait")); return; }
    busy = L("正在连接 ZeroTier…", "Contacting ZeroTier…");
    auto d = std::make_shared<ZtJoinData>();
    const std::string net = zt_net;
    const bool host = zt_host;
    const std::string own = zt_own;
    async.start([this, d, net, leave_if_joined, host, own] {
        if (async.cancelled()) return;
        if (!zt_valid_id(net)) { d->err = 1; return; }
        if (!zt_installed()) { d->err = 2; return; }
        bool tun_missing = !zt_tun_ok();
        if (!zt_tun_ensure()) { d->err = 3; return; }
        if (tun_missing && zt_daemon_up()) zt_restart_daemon();
        if (!zt_ensure_daemon()) { d->err = 4; return; }
        if (async.cancelled()) return;
        ZtStatus st = zt_status(net);
        if (leave_if_joined && st.found && st.ok && !(host && net == own)) {
            zt_cli("leave", net);
            d->left = true;
        } else {
            zt_cli("join", net);
        }
        usleep(400000);
        d->st = zt_status(net);
        d->ok = true;
    }, [this, d] {
        busy.clear();
        if (!d->ok) {
            switch (d->err) {
            case 1: show_toast(L("网络 ID 需 16 位十六进制", "Network ID must be 16 hex digits")); break;
            case 2: show_toast(L("未找到 zerotier-one", "zerotier-one not installed")); break;
            case 3: show_toast(L("无法加载 TUN (tun.ko)", "Cannot load TUN (tun.ko)")); break;
            default: show_toast(L("ZeroTier 启动失败", "ZeroTier failed to start")); zt.st.state = "down"; break;
            }
            return;
        }
        zt.st = d->st;
        if (d->left)
            show_toast(L("已离开 ZeroTier 网络", "Left ZeroTier network"));
        else if (d->st.ok)
            show_toast(L("已连接", "Connected") + (d->st.ip.empty() ? "" : " " + d->st.ip));
        else
            show_toast(d->st.found ? L("等待主机授权（按 A 刷新）", "Waiting for host approval (A to refresh)")
                                   : L("已请求加入，等待主机授权", "Join requested; waiting for approval"));
        save_cfg();
    });
}

void App::zt_action() { zt_join_current(true); }

struct ShortJoinData {
    bool ok = false;
    std::string nwid, err;
};

void App::zt_join_by_code(const std::string &code) {
    if (async.busy()) { show_toast(L("正在处理，请稍候", "Busy, please wait")); return; }
    if (short_url.empty()) {
        show_toast(L("未设置短码服务地址", "Short-code service not configured"));
        return;
    }
    busy = L("正在解析短码…", "Resolving code…");
    auto d = std::make_shared<ShortJoinData>();
    async.start([this, code, d] {
        if (async.cancelled()) return;
        d->ok = short_resolve(code, d->nwid, d->err);
    }, [this, d] {
        busy.clear();
        if (!d->ok) {
            show_toast(d->err.empty() ? L("短码无效", "Invalid code") : d->err);
            return;
        }
        zt_net = d->nwid;
        save_cfg();
        show_toast(L("短码 → ", "Code → ") + d->nwid);
        zt_join_current(false);
    });
}

struct ShortPubData {
    bool ok = false;
    std::string code, err;
    long long exp = 0;
};

void App::short_publish_own(bool silent) {
    const std::string nw = (zt_host && zt_valid_id(zt_own)) ? zt_own : zt_net;
    if (!zt_valid_id(nw)) {
        show_toast(L("先创建或加入一个网络", "Create or join a network first"));
        return;
    }
    if (short_url.empty()) {
        if (!silent) show_toast(L("先设置短码服务地址", "Set the short-code service URL first"));
        return;
    }
    if (async.busy()) {
        if (silent) short_pub_pending = true;
        else show_toast(L("正在处理，请稍候", "Busy, please wait"));
        return;
    }
    if (!silent) busy = L("正在生成短码…", "Publishing code…");
    auto d = std::make_shared<ShortPubData>();
    async.start([this, nw, d] {
        if (async.cancelled()) return;
        d->ok = short_publish(nw, d->code, d->err, &d->exp);
    }, [this, d, silent] {
        if (!silent) busy.clear();
        if (!d->ok) {
            if (!silent)
                show_toast(d->err.empty() ? L("短码生成失败", "Publish failed") : d->err);
            return;
        }
        zt_short = d->code;
        zt_short_exp = d->exp;
        save_cfg();
        show_toast(L("短码 ", "Code ") + d->code + L("（10 分钟内有效）", " (valid 10 min)"));
    });
}
