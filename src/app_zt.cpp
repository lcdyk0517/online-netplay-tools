#include "app.h"

#include <algorithm>
#include <string>
#include <unistd.h>

#include "config.h"
#include "i18n.h"
#include "theme.h"

void App::zt_adopt_own() {
    if (zt_host || !zt_installed() || !zt_daemon_up()) return;
    std::vector<std::string> nets = zt_controller_networks();
    if (nets.empty()) return;
    zt_own = nets[0];
    zt_host = true;
    save_cfg();
}

void App::zt_refresh_members() {
    if (!zt_host || !zt_valid_id(zt_own)) { zt_members_list.clear(); return; }
    zt_members_list = zt_fetch_members(zt_own);
    if (zt_msel >= (int)zt_members_list.size()) zt_msel = 0;
}

int App::zt_pending() const {
    int n = 0;
    for (auto &m : zt_members_list) if (!m.authorized) n++;
    return n;
}

void App::zt_create_own() {
    busy = L("正在创建 ZeroTier 网络…", "Creating ZeroTier network…");
    draw();
    struct BusyGuard { std::string &b; ~BusyGuard() { b.clear(); } } guard{busy};
    if (!zt_installed()) { show_toast(L("未找到 zerotier-one", "zerotier-one not installed")); return; }
    if (!zt_tun_ensure()) { show_toast(L("无法加载 TUN (tun.ko)", "Cannot load TUN (tun.ko)")); return; }
    if (!zt_ensure_daemon()) { show_toast(L("ZeroTier 启动失败", "ZeroTier failed to start")); return; }
    ZtStatus st = zt_status("");
    if (st.node.size() != 10) { show_toast(L("读取节点 ID 失败", "Cannot read node ID")); return; }
    std::string nw = zt_create_network(st.node);
    if (nw.empty()) { show_toast(L("创建网络失败", "Network creation failed")); return; }
    zt_net = nw;
    zt_own = nw;
    zt_host = true;
    save_cfg();   /* keep the host role even if join/authorize fails */
    zt_cli("join " + zt_net);
    zt_authorize_member(zt_own, st.node, true);   // authorize ourselves
    save_cfg();
    usleep(300000);
    zt_refresh_members();
    zt_st = zt_status(zt_net);
    show_toast(L("已创建网络 ", "Network created ") + nw);
}

void App::open_member_screen() {
    zt_refresh_members();
    zt_msel = 0;
    zt_mscroll = 0;
    screen = 5;
}

void App::member_move(int d) {
    int n = (int)zt_members_list.size();
    if (!n) return;
    zt_msel = std::max(0, std::min(n - 1, zt_msel + d));
    int vis = std::max(3, (SH - 220) / 44);
    if (zt_msel < zt_mscroll) zt_mscroll = zt_msel;
    if (zt_msel >= zt_mscroll + vis) zt_mscroll = zt_msel - vis + 1;
}

void App::member_toggle() {
    if (zt_msel >= (int)zt_members_list.size()) return;
    const ZtMember &m = zt_members_list[zt_msel];
    zt_authorize_member(zt_own, m.id, !m.authorized);
    zt_refresh_members();
}

void App::member_delete() {
    if (zt_msel >= (int)zt_members_list.size()) return;
    std::string id = zt_members_list[zt_msel].id;
    if (zt_confirm == "del:" + id && SDL_GetTicks() < zt_confirm_until) {
        zt_delete_member(zt_own, id);
        zt_confirm.clear();
        zt_refresh_members();
        show_toast(L("已删除成员 ", "Removed member ") + id);
    } else {
        zt_confirm = "del:" + id;
        zt_confirm_until = SDL_GetTicks() + 3000;
        show_toast(L("再按一次 X 删除该成员", "Press X again to remove"));
    }
}

void App::member_clear() {
    if (zt_confirm == "clear" && SDL_GetTicks() < zt_confirm_until) {
        for (auto &m : zt_members_list) zt_delete_member(zt_own, m.id);
        zt_confirm.clear();
        zt_refresh_members();
        show_toast(L("已清空所有成员", "All members removed"));
    } else {
        zt_confirm = "clear";
        zt_confirm_until = SDL_GetTicks() + 3000;
        show_toast(L("再按一次 Start 清空全部成员", "Press Start again to clear all"));
    }
}

std::string App::zt_state_text() const {
    const std::string &s = zt_st.state;
    if (s == "noinst") return L("未安装", "Not installed");
    if (s == "notun") return L("内核未启用 TUN", "Kernel TUN off");
    if (s == "down") return L("未启动", "Stopped");
    if (s == "notjo") return L("未加入", "Not joined");
    if (s == "notfound") return L("网络不存在", "Network not found");
    if (s == "denied") return L("未授权", "Access denied");
    if (s == "requesting") return L("正在获取配置", "Requesting config");
    if (s == "ok") return L("已连接", "Connected") + (zt_st.ip.empty() ? "" : " " + zt_st.ip);
    return s.empty() ? L("未启动", "Stopped") : s;
}

void App::zt_join_current(bool leave_if_joined) {
    busy = L("正在连接 ZeroTier…", "Contacting ZeroTier…");
    draw();
    struct BusyGuard { std::string &b; ~BusyGuard() { b.clear(); } } guard{busy};
    if (!zt_valid_id(zt_net)) {
        show_toast(L("网络 ID 需 16 位十六进制", "Network ID must be 16 hex digits"));
        return;
    }
    if (!zt_installed()) { show_toast(L("未找到 zerotier-one", "zerotier-one not installed")); return; }
    if (!zt_tun_ensure()) { show_toast(L("无法加载 TUN (tun.ko)", "Cannot load TUN (tun.ko)")); return; }
    if (!zt_ensure_daemon()) { show_toast(L("ZeroTier 启动失败", "ZeroTier failed to start")); zt_st.state = "down"; return; }
    zt_st = zt_status(zt_net);
    if (leave_if_joined && zt_st.found && zt_st.ok && !(zt_host && zt_net == zt_own)) {
        zt_cli("leave " + zt_net);
        show_toast(L("已离开 ZeroTier 网络", "Left ZeroTier network"));
    } else {
        zt_cli("join " + zt_net);
        show_toast(zt_st.found ? L("等待主机授权（按 A 刷新）", "Waiting for host approval (A to refresh)")
                               : L("已请求加入，等待主机授权", "Join requested; waiting for approval"));
    }
    usleep(400000);
    zt_st = zt_status(zt_net);
    save_cfg();
}

void App::zt_action() { zt_join_current(true); }
