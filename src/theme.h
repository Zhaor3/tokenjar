#pragma once
#include <lvgl.h>

namespace Theme {

// ── Colors ───────────────────────────────────────────────────────
namespace Color {
    inline lv_color_t bg()       { return lv_color_hex(0x000000); }
    inline lv_color_t text()     { return lv_color_hex(0xf5f1ea); }
    inline lv_color_t dim()      { return lv_color_hex(0x6b6660); }
    inline lv_color_t vdim()     { return lv_color_hex(0x3d3a37); }
    inline lv_color_t hairline() { return lv_color_hex(0x1a1a1a); }
    inline lv_color_t claude()   { return lv_color_hex(0xd97757); }
    inline lv_color_t openai()   { return lv_color_hex(0x10a37f); }
    inline lv_color_t combined() { return lv_color_hex(0xc9b8e8); }
    inline lv_color_t ok()       { return lv_color_hex(0x4ade80); }
    inline lv_color_t warn()     { return lv_color_hex(0xfbbf24); }
    inline lv_color_t err()      { return lv_color_hex(0xef4444); }

    inline lv_color_t darken(lv_color_t c, uint8_t pct) {
        return lv_color_mix(c, lv_color_black(), 255 - (255 * pct / 100));
    }
}

// ── Fonts (built-in Montserrat; swap for Inter / JetBrains Mono) ─
namespace Font {
    inline const lv_font_t* hero()  { return &lv_font_montserrat_48; }
    inline const lv_font_t* body()  { return &lv_font_montserrat_14; }
    inline const lv_font_t* label() { return &lv_font_montserrat_12; }
    inline const lv_font_t* small() { return &lv_font_montserrat_10; }
}

// ── Spacing ──────────────────────────────────────────────────────
namespace Sp {
    constexpr int pad       = 16;
    constexpr int inner     = 8;
    constexpr int bar_h     = 4;
    constexpr int dot_r     = 3;   // radius → 6 px diameter
    constexpr int hairline  = 1;
}

// ── Animation durations (ms) ─────────────────────────────────────
namespace Anim {
    constexpr int transition = 250;
    constexpr int sparkline  = 300;
    constexpr int countup    = 400;
    constexpr int press      = 120;
    constexpr int bl_fade    = 200;
}

} // namespace Theme
