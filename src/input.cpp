#include "app.h"

#include <SDL2/SDL.h>
#include <algorithm>

#include "config.h"
#include "i18n.h"

int run_app(App &app) {
    SDL_GameController *pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); if (pad) break; }
    }
    if (pad) app.pad_name = SDL_GameControllerName(pad) ? SDL_GameControllerName(pad) : "pad";
    else app.show_toast(L("未检测到手柄", "No gamepad detected"));

    bool sel_held = false, start_held = false;
    bool run = true;
    auto nav = [&](int d) {
        int n = (int)app.filtered.size();
        if (n == 0) return;
        app.sel = std::max(0, std::min(n - 1, app.sel + d));
        if (app.sel < app.scroll) app.scroll = app.sel;
        if (app.sel >= app.scroll + app.rows) app.scroll = app.sel - app.rows + 1;
    };
    auto act = [&](int a) {  // 0 join, 1 quit, 2 filter, 3 refresh, 4 language
        switch (a) {
        case 0: if (!app.filtered.empty()) app.request_join(app.rooms[app.filtered[app.sel]]); break;
        case 1: run = false; break;

        case 3: app.refresh(); break;
        }
    };
    auto check_exit_combo = [&]() { if (sel_held && start_held) run = false; };

    auto back = [&]() {
        if (app.screen == 6) { app.screen = app.choice.ret; return; }
        if (app.screen == 5) { app.screen = 1; return; }
        if (app.screen == 4) app.screen = 3;
        else if (app.screen == 3) app.screen = 0;
        else if (app.screen == 1) app.screen = 0;
        else if (app.screen == 2) app.editor_cancel();
        else run = false;
    };
    auto vert = [&](int d) {
        if (app.screen == 1) app.settings_move(d);
        else if (app.screen == 2) app.editor_move(0, d);
        else if (app.screen == 3) app.host_move(d);
        else if (app.screen == 4) app.picker_move(d);
        else if (app.screen == 5) app.member_move(d);
        else if (app.screen == 6) app.choice_move(d);
        else nav(d);
    };
    auto horiz = [&](int d) { if (app.screen == 2) app.editor_move(d, 0); };
    auto confirm = [&]() {  // A / Enter
        if (app.screen == 1) app.settings_change();
        else if (app.screen == 2) app.editor_activate();
        else if (app.screen == 3) app.host_change();
        else if (app.screen == 4) app.picker_choose();
        else if (app.screen == 5) app.member_toggle();
        else if (app.screen == 6) app.choice_choose();
        else act(0);
    };
    auto start_btn = [&]() {
        if (app.screen == 0) app.open_settings();
        else if (app.screen == 1) { app.set.sel = 8; app.settings_change(); }  /* Start = save */
        else if (app.screen == 2) app.editor_finish();
        else if (app.screen == 3) app.host_launch();
        else if (app.screen == 4) app.picker_choose();
        else if (app.screen == 5) app.member_clear();
        else if (app.screen == 6) app.choice_choose();
    };
    auto host_open = [&]() { if (app.screen == 0) { app.screen = 3; app.host.row = 0; } };
    auto x_btn = [&]() {
        if (app.screen == 2) app.editor_cancel();      // discard edit
        else if (app.screen == 5) app.member_delete();
        else if (app.screen == 0) host_open();
    };
    auto y_btn = [&]() {
        if (app.screen == 2) app.editor_finish();
        else if (app.screen == 5) app.zt_refresh_members();
        else if (app.screen == 0) act(3);
    };
    auto b_btn = [&]() {  // B: exits the list, deletes a char in the editor
        if (app.screen == 2) { if (!app.set.buf.empty()) app.set.buf.pop_back(); }
        else back();
    };
    auto page_btn = [&](int d) { if (app.screen == 0) app.set_page(app.filter + d); };

    // rk3562-joystick reports the left stick as an axis only (the d-pad is a
    // separate set of buttons), so stick navigation needs its own repeat path.
    int stick_dir = 0, hstick_dir = 0;
    Uint32 stick_next = 0, hstick_next = 0;
    auto set_stick = [&](int v, Uint32 now) {
        int d = v < -8000 ? -1 : (v > 8000 ? 1 : 0);
        if (d != stick_dir) {
            stick_dir = d;
            if (d) { vert(d); stick_next = now + 350; }
        }
    };
    auto set_hstick = [&](int v, Uint32 now) {
        int d = v < -8000 ? -1 : (v > 8000 ? 1 : 0);
        if (d != hstick_dir) {
            hstick_dir = d;
            if (d) { horiz(d); hstick_next = now + 350; }
        }
    };

    while (run) {
        app.poll_async();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { run = false; break; }
            /* Only modal work (create/join) takes over input; ordinary
             * background refreshes must not swallow key presses. */
            if (!app.busy.empty()) continue;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) back();
                else if (k == SDLK_TAB) start_btn();
                else if (k == SDLK_DOWN) vert(1);
                else if (k == SDLK_UP) vert(-1);
                else if (k == SDLK_RIGHT) horiz(1);
                else if (k == SDLK_LEFT) horiz(-1);
                else if (k == SDLK_BACKSPACE) { if (app.screen == 2 && !app.set.buf.empty()) app.set.buf.pop_back(); }
                else if (k == SDLK_RETURN || k == SDLK_SPACE) confirm();
                else if (app.screen == 0) {
                    if (k == SDLK_x) host_open();
                    else if (k == SDLK_y) act(3);
                    else if (k == SDLK_l) page_btn(-1);
                    else if (k == SDLK_r) page_btn(1);
                }
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {
                bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) sel_held = down;
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) start_held = down;
                check_exit_combo();
                if (!down) continue;
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: vert(1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: vert(-1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: horiz(1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: horiz(-1); break;
                case SDL_CONTROLLER_BUTTON_A: confirm(); break;
                case SDL_CONTROLLER_BUTTON_B: b_btn(); break;
                case SDL_CONTROLLER_BUTTON_X: x_btn(); break;
                case SDL_CONTROLLER_BUTTON_Y: y_btn(); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: page_btn(-1); break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: page_btn(1); break;
                case SDL_CONTROLLER_BUTTON_START: if (!sel_held) start_btn(); break;
                default: break;
                }
            } else if (e.type == SDL_CONTROLLERAXISMOTION) {
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) set_stick(e.caxis.value, SDL_GetTicks());
                else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) set_hstick(e.caxis.value, SDL_GetTicks());
            }
        }
        Uint32 now = SDL_GetTicks();
        if (stick_dir && now >= stick_next) { vert(stick_dir); stick_next = now + 130; }
        if (hstick_dir && now >= hstick_next) { horiz(hstick_dir); hstick_next = now + 130; }
        if (app.screen == 0 && app.loaded && now - app.last_refresh > 30000) app.refresh();
        app.draw();
        SDL_Delay(16);
    }
    if (pad) SDL_GameControllerClose(pad);
    // Persist anything still in memory even if the user exits without the save row.
    save_cfg();
    save_ra_all();
    return 0;
}
