#pragma once

#include <SDL2/SDL.h>

// Console geometry comes from console_detect so the UI matches every panel
// (AIS/ngp45ht/rg43*=1024x768, xf55h/rg52mini=1280x720).
extern int SW, SH;

void detect_screen();

extern const SDL_Color BG, CARD, TXT, MUTED, LINE, ACCENT, ACCENT_D, WARN, BAD, CHIP,
    SEL_BG, DIM;
