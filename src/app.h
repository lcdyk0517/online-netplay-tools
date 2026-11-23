#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "async.h"
#include <map>
#include <string>
#include <vector>

#include "content.h"
#include "lobby.h"
#include "zerotier.h"

extern const char *OSK_ROWS[5];
extern const char *NETID_ROWS[2];

struct App {
    AsyncTask async;
    bool zt_refresh_pending = false;
    bool lan_scan_pending = false;
    bool lobby_refresh_pending = false;
    bool short_pub_pending = false;

    ~App();
    void close_fonts();
    SDL_Renderer *r = nullptr;
    TTF_Font *f28 = nullptr, *f22 = nullptr, *f18 = nullptr, *f16 = nullptr;
    std::vector<Room> rooms;
    std::map<std::string, std::string> names, crcs;
    std::map<std::string, CoreRef> cores;
    std::vector<int> filtered;
    int filter = 0, sel = 0, scroll = 0;
    bool loading = false, loaded = false;
    std::string status;
    Uint32 last_refresh = 0;
    std::string toast;
    Uint32 toast_until = 0;
    std::string busy;   // modal "please wait" text while a blocking action runs
    std::string pad_name;
    int rows = 6;

    // 0 list, 1 settings, 2 on-screen keyboard, 3 host, 4 game picker, 5 core picker
    int screen = 0;
    std::string join_pass; // room password typed in our own OSK
    Room pending_room;
    bool has_pending = false;

    struct ZtUi {
        ZtStatus st;
        std::vector<ZtMember> members;
        int msel = 0, mscroll = 0;
        std::string confirm;          // pending destructive action (double press)
        Uint32 confirm_until = 0;
        bool join_after_edit = false;
    } zt;

    struct HostUi {
        std::vector<EsSystem> systems;    // hostable systems (es_systems order, sorted by alias)
        std::vector<GameEntry> games;     // games of the selected system
        std::vector<std::string> cores;   // hostable cores of the selected system
        std::map<std::string, bool> dir_cache;
        int sys = -1;
        int row = 0;
        bool lan_only = false;
        bool pass_on = false;             // require a password for the room we host
        std::string pass;
        std::string game_name, game_path, core;
        int pick_kind = 0;                // 0 system, 1 game, 2 core
        int pick_sel = 0, pick_scroll = 0;
    } host;

    struct SettingsUi {
        int sel = 0;
        int target = 0;
        std::string buf;
        int row = 0, col = 0;
    } set;

    // Generic list popup for the RetroArch relay location.
    struct ChoiceUi {
        int ret = 1;      // screen to return to
        int sel = 0, scroll = 0;
    } choice;

    void text(TTF_Font *f, const std::string &s, int x, int y, SDL_Color c, int maxw = 0);
    void fill(SDL_Rect rc, SDL_Color c);
    void round(SDL_Rect rc, int rad, SDL_Color c);

    // ZeroTier
    std::string zt_target() const;
    void poll_async();
    void refresh_zt_async();
    void zt_refresh_members();
    int zt_pending() const;
    void zt_create_own();
    void open_member_screen();
    void member_move(int d);
    void member_toggle();
    void member_delete();
    void member_clear();
    std::string zt_state_text() const;
    void zt_join_current(bool leave_if_joined);
    void zt_action();
    // 6-char lookup code -> 16-hex network id, then join.
    void zt_join_by_code(const std::string &code);
    void short_publish_own(bool silent = false);

    // settings / on-screen keyboard
    void open_settings();
    int relay_idx() const;
    void set_relay_idx(int i);
    std::string relay_label(int i) const;
    void open_choice(int ret_screen);
    void choice_move(int d);
    void choice_choose();
    std::vector<int> settings_rows() const;
    void settings_move(int d);
    void open_editor(int target);
    void settings_change();
    void editor_move(int dx, int dy);
    void editor_finish();
    void editor_cancel();
    void editor_activate();

    // hosting (driven by es_systems.cfg)
    void build_systems();
    void build_sys_games();
    void select_system(int idx);
    std::string system_alias() const;
    int picker_count() const;
    void picker_move(int d);
    void picker_choose();
    void host_move(int d);
    void host_change();
    void host_launch();

    // lobby list
    void apply_filter();
    void set_page(int p);
    void scan_lan();
    void refresh();
    void show_toast(const std::string &s);
    void request_join(const Room &rm);
    void join_room(const Room &rm, const std::string &pass);

    // drawing
    void draw();
    void draw_settings();
    void draw_editor();
    void draw_members();
    void draw_host();
    void draw_picker();
    void draw_choice();
};
