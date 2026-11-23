#include "app.h"

#include <string>

#include "i18n.h"
#include "theme.h"

const char *OSK_ROWS[5] = {
    "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "abcdefghijklm", "nopqrstuvwxyz", "0123456789-.:" };

void App::text(TTF_Font *f, const std::string &s, int x, int y, SDL_Color c, int maxw) {
    if (s.empty()) return;
    std::string out = s;
    if (maxw > 0) {
        int w = 0; TTF_SizeUTF8(f, out.c_str(), &w, nullptr);
        while (w > maxw && out.size() > 1) {
            out.erase(out.size() - (out.size() > 1 && (unsigned char)out[out.size()-1] >= 0x80 ? 3 : 1));
            TTF_SizeUTF8(f, out.c_str(), &w, nullptr);
        }
    }
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, out.c_str(), c);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_Rect dr{x, y, surf->w, surf->h};
    SDL_RenderCopy(r, tex, nullptr, &dr);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

void App::fill(SDL_Rect rc, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rc);
}

void App::round(SDL_Rect rc, int rad, SDL_Color c) {
    fill(rc, c);
    for (int i = 0; i < rad; i++) {
        SDL_Rect l{rc.x + i, rc.y + rad - i, rc.w - 2 * i, 1};
        fill(l, c);
        SDL_Rect rr{rc.x + i, rc.y + rc.h - rad + i - 1, rc.w - 2 * i, 1};
        fill(rr, c);
    }
}
// ES owns the UI language (the same source the standalone launchers read):
// ~/.emulationstation/es_settings.cfg, with the .cn marker as a fallback.

void App::close_fonts() {
    if (f28) TTF_CloseFont(f28);
    if (f22) TTF_CloseFont(f22);
    if (f18) TTF_CloseFont(f18);
    if (f16) TTF_CloseFont(f16);
    f28 = f22 = f18 = f16 = nullptr;
}

App::~App() {
    async.cancel();
    async.join();
    close_fonts();
}
