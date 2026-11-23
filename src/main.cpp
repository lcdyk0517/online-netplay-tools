#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <curl/curl.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

#include "app.h"
#include "config.h"
#include "content.h"
#include "i18n.h"
#include "lobby.h"
#include "theme.h"
#include "zerotier.h"

int run_app(App &app);

int main(int argc, char **argv) {
    detect_screen();
#ifdef RA_SHOT
    if (const char *e = getenv("RA_SCREEN_W")) SW = atoi(e);
    if (const char *e = getenv("RA_SCREEN_H")) SH = atoi(e);
#endif
    // Headless local game index check: netplay --games | --find <name>
    if (argc > 1 && (std::string(argv[1]) == "--games" || std::string(argv[1]) == "--find")) {
        std::map<std::string, std::string> gnames, gcrcs;
        scan_es_games(gnames);
        scan_playlists(gcrcs);
        if (std::string(argv[1]) == "--games") {
            printf("%zu local game keys\n", gnames.size());
            return 0;
        }
        if (argc < 3) return 2;
        LocalGame g = find_game(argv[2], "", gnames, gcrcs);
        printf("%s\n", g.found ? g.path.c_str() : "NOT FOUND");
        return g.found ? 0 : 1;
    }
    // Headless ZeroTier start for SSH debugging: netplay --zt-start
    if (argc > 1 && std::string(argv[1]) == "--zt-start") {
        SDL_Init(SDL_INIT_TIMER);
        bool tun = zt_tun_ensure();
        bool ok = tun && zt_ensure_daemon();
        printf("zt_start=%d installed=%d tun=%d\n", (int)ok, (int)zt_installed(), (int)zt_tun_ok());
        ZtStatus st = zt_status("");
        printf("node=%s state=%s\n", st.node.c_str(), st.state.c_str());
        return ok ? 0 : 1;
    }
    // Headless LAN discovery for SSH debugging: ra-lobby --lan
    if (argc > 1 && std::string(argv[1]) == "--lan") {
        SDL_Init(SDL_INIT_TIMER);
        std::vector<Room> found;
        lan_discover(found);
        for (auto &rm : found)
            printf("%-32s | %-22s | %s:%d | pw=%d | %s\n", rm.gamename.c_str(),
                   rm.corename.c_str(), rm.address.c_str(), rm.port,
                   (int)rm.has_password, rm.nickname.c_str());
        printf("%zu lan hosts\n", found.size());
        return 0;
    }
    // Headless lobby check for SSH debugging: ra-lobby --dump
    if (argc > 1 && std::string(argv[1]) == "--dump") {
        App app;
        load_language();
        load_cfg();
        load_ra_all();
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::string body, err;
        if (!http_get(LOBBY_URL, body, err)) { fprintf(stderr, "fetch failed: %s\n", err.c_str()); return 1; }
        std::vector<Room> rooms = parse_lobby(body);
        for (const Room &rm : rooms)
            printf("%-30s | %-20s | %s:%d | p=%d | pw=%d | conn=%d | mitm=%s:%d %s\n",
                   rm.gamename.c_str(), rm.corename.c_str(), rm.address.c_str(), rm.port,
                   rm.players, (int)rm.has_password, (int)rm.connectable,
                   rm.mitm_ip.c_str(), rm.mitm_port, rm.mitm_session.c_str());
        printf("%zu rooms\n", rooms.size());
        return 0;
    }
#ifdef RA_SHOT
    bool shot_mode = getenv("RA_LOBBY_SHOT") != nullptr;
#else
    const bool shot_mode = false;
#endif
    if (SDL_Init(shot_mode ? SDL_INIT_EVENTS
                           : (SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER)) != 0) return 1;
    /* Pad mapping comes from the environment set by the launcher:
     * SDL_GAMECONTROLLERCONFIG_FILE (ArkOS exports the per-device DB) or
     * SDL_GAMECONTROLLERCONFIG (read by SDL itself). No device-specific
     * button table lives in this binary. */
    const char *pad_db = getenv("SDL_GAMECONTROLLERCONFIG_FILE");
    if (pad_db && *pad_db && SDL_GameControllerAddMappingsFromFile(pad_db) < 0)
        fprintf(stderr, "netplay: cannot load %s\n", pad_db);
    if (TTF_Init() != 0) return 1;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    SDL_Window *w = nullptr;
    SDL_Renderer *r = nullptr;
    SDL_Surface *shot_surf = nullptr;
    if (shot_mode) {
        shot_surf = SDL_CreateRGBSurfaceWithFormat(0, SW, SH, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!shot_surf) return 1;
        r = SDL_CreateSoftwareRenderer(shot_surf);
    } else {
        w = SDL_CreateWindow("netplay", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SW, SH,
                             SDL_WINDOW_FULLSCREEN);
        if (!w) return 1;
        r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!r) r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!r) return 1;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const char *font = "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc";
#ifdef RA_SHOT
    if (const char *f = getenv("RA_FONT")) font = f;
#endif
    if (access(font, R_OK) != 0) font = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    App app; app.r = r;
    app.f28 = TTF_OpenFont(font, 28); app.f22 = TTF_OpenFont(font, 22);
    app.f18 = TTF_OpenFont(font, 18); app.f16 = TTF_OpenFont(font, 16);
    auto cleanup = [&]() {
        app.async.cancel();
        app.async.join();
        app.close_fonts();
        if (r) SDL_DestroyRenderer(r);
        if (w) SDL_DestroyWindow(w);
        if (shot_surf) SDL_FreeSurface(shot_surf);
        TTF_Quit();
        curl_global_cleanup();
        SDL_Quit();
    };
    if (!app.f28 || !app.f22 || !app.f18 || !app.f16) { cleanup(); return 1; }
    load_language();
    load_cfg();
#ifdef RA_SHOT
    if (getenv("RA_LOBBY_ZH")) zh = true;
#endif
    load_ra_all();
    app.refresh_zt_async();  /* adopt/keep the hosted network if the daemon runs */
    scan_es_games(app.names);
    scan_playlists(app.crcs);
    app.cores = scan_cores();
    app.build_systems();
    app.refresh();

#ifdef RA_SHOT
    if (const char *flt = getenv("RA_LOBBY_FILTER")) {
        app.filter = atoi(flt);
        app.apply_filter();
    }
    if (const char *sc = getenv("RA_LOBBY_SHOT_SCREEN")) {
        if (getenv("RA_LOBBY_ZT")) {
            zt_net = "0c6e45f0ae1cdd3e";
            zt_host = getenv("RA_LOBBY_ZTHOST") != nullptr;
            if (zt_host) zt_own = zt_net;
            if (const char *s = getenv("RA_LOBBY_SHORTURL")) short_url = s;
            if (const char *c = getenv("RA_LOBBY_ZTSHORT")) zt_short = c;
        }
        app.screen = atoi(sc);
        if (app.screen == 1) { app.open_settings(); app.set.sel = 1; }
        if (app.screen == 2) {
            int et = getenv("RA_LOBBY_EDITOR_TARGET") ? atoi(getenv("RA_LOBBY_EDITOR_TARGET")) : 0;
            app.open_editor(et);
            if (et == 2) app.set.buf = "15228b9233";
        }
        if (app.screen == 5) app.zt.members = { { "15228b9233", "10.210.41.129", true },
                                                { "abcdef0123", "", false } };
        if (app.screen == 6) app.open_choice(1);
        if (app.screen == 3 || app.screen == 4) {
            if (app.host.systems.empty()) {
                EsSystem sy;
                sy.name = "snes"; sy.fullname = "Super NES"; sy.path = "/roms/snes/";
                sy.hostable = { "snes9x", "snes9x2005" };
                app.host.systems.push_back(sy);
                sy.name = "gba"; sy.fullname = "Game Boy Advance"; sy.path = "/roms/gba/";
                sy.hostable = { "armgba", "mgba" };
                app.host.systems.push_back(sy);
            }
            if (app.host.sys < 0) app.select_system(0);
            if (getenv("RA_LOBBY_LANONLY")) app.host.lan_only = true;
            if (app.screen == 4) {
                app.host.pick_kind = getenv("RA_LOBBY_SHOT_PICK") ? atoi(getenv("RA_LOBBY_SHOT_PICK")) : 0;
                if (app.host.pick_kind == 1 && app.host.games.empty())
                    for (int i = 0; i < 12; i++)
                        app.host.games.push_back({ "Sample Game " + std::to_string(i + 1),
                                                  "/roms/snes/sample" + std::to_string(i) + ".sfc" });
                if (app.host.pick_kind == 2 && app.host.cores.empty())
                    app.host.cores = { "snes9x", "snes9x2005", "chimerasnes" };
            }
        }
        if (getenv("RA_LOBBY_ZT")) {
            zt_net = "0c6e45f0ae1cdd3e";
            zt_host = getenv("RA_LOBBY_ZTHOST") != nullptr;
            if (zt_host) zt_own = zt_net;
            if (const char *s = getenv("RA_LOBBY_SHORTURL")) short_url = s;
            if (const char *c = getenv("RA_LOBBY_ZTSHORT")) zt_short = c;
            app.zt.st.state = "ok";
            app.zt.st.node = "0c6e45f0ae";
            app.zt.st.ip = "10.147.17.5";
            if (zt_host) {
                app.zt.members.clear();
                app.zt.members.push_back({ "1234567890", "10.147.17.99", true });
                app.zt.members.push_back({ "abcdef0123", "", false });
            }
        }
        app.draw();
    }
    if (const char *shot = getenv("RA_LOBBY_SHOT")) {
        for (int i = 0; i < 400 && (app.async.busy() || app.zt_refresh_pending ||
                                   app.lan_scan_pending || app.lobby_refresh_pending); i++) {
            app.poll_async();
            SDL_Delay(50);
        }
        app.draw();
        if (shot_surf) SDL_SaveBMP(shot_surf, shot);
        cleanup();
        return 0;
    }
#endif
    int rc = run_app(app);
    cleanup();
    return rc;
}
