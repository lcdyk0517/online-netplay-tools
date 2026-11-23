#include "theme.h"

#include <cstdio>

const SDL_Color BG{244,246,249,255}, CARD{255,255,255,255}, TXT{27,36,48,255},
    MUTED{107,122,140,255}, LINE{227,232,239,255}, ACCENT{19,183,166,255},
    ACCENT_D{12,130,118,255}, WARN{233,161,59,255}, BAD{226,92,92,255}, CHIP{238,243,247,255},
    SEL_BG{224,246,242,255}, DIM{10,16,24,160};

int SW = 1280, SH = 720;
void detect_screen() {
    FILE *p = popen("/usr/local/bin/console_detect -s 2>/dev/null", "r");
    if (!p) return;
    char line[256];
    int w = 0, h = 0;
    while (fgets(line, sizeof line, p)) {
        sscanf(line, "SCREEN_WIDTH=%d", &w);
        sscanf(line, "SCREEN_HEIGHT=%d", &h);
    }
    pclose(p);
    if (w >= 320 && h >= 240 && w <= 4096 && h <= 4096) { SW = w; SH = h; }
}
