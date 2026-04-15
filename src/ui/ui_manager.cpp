#include "ui/ui_manager.h"
#include "ui/screen_provider.h"
#include "ui/screen_provider_h.h"
#include "ui/screen_settings.h"
#include "ui/screen_settings_h.h"
#include "storage/settings_store.h"
#include "config.h"
#include "theme.h"
#include <qrcode.h>
#include <Arduino.h>

using namespace Theme;

// ── Initialization ───────────────────────────────────────────────

// Construct a provider screen in the appropriate orientation.
static IScreenProvider* makeProviderScreen(bool horizontal, const char* name, lv_color_t accent) {
    if (horizontal) return new ScreenProviderH(name, accent);
    return new ScreenProvider(name, accent);
}

void UIManager::init(SettingsStore& store, bool horizontal) {
    horizontal_ = horizontal;
    bool has_c = store.hasClaude();
    bool has_o = store.hasOpenAI();
    num_modes_ = 0;

    if (has_c) {
        scr_claude_today_ = makeProviderScreen(horizontal, "CLAUDE", Color::claude());
        scr_claude_month_ = makeProviderScreen(horizontal, "CLAUDE", Color::claude());
        modes_[num_modes_++] = Mode::CLAUDE_TODAY;
        modes_[num_modes_++] = Mode::CLAUDE_MONTH;
    }
    if (has_o) {
        scr_openai_today_ = makeProviderScreen(horizontal, "OPENAI", Color::openai());
        scr_openai_month_ = makeProviderScreen(horizontal, "OPENAI", Color::openai());
        modes_[num_modes_++] = Mode::OPENAI_TODAY;
        modes_[num_modes_++] = Mode::OPENAI_MONTH;
    }
    if (has_c && has_o) {
        scr_combined_ = makeProviderScreen(horizontal, "COMBINED", Color::combined());
        modes_[num_modes_++] = Mode::COMBINED;
    }

    scr_settings_ = horizontal
        ? static_cast<IScreenSettings*>(new ScreenSettingsH())
        : static_cast<IScreenSettings*>(new ScreenSettings());
    modes_[num_modes_++] = Mode::SETTINGS;

    cur_idx_   = 0;
    bl_target_ = BL_FULL;
    bl_current_= BL_FULL;
    last_act_  = millis();
    analogWrite(PIN_TFT_BL, BL_FULL);

    if (num_modes_ > 0) {
        lv_obj_t* first = screenForMode(modes_[0]);
        if (first) lv_screen_load(first);
    }
}

// ── Screen lookup ────────────────────────────────────────────────

lv_obj_t* UIManager::screenForMode(Mode m) {
    switch (m) {
        case Mode::CLAUDE_TODAY:  return scr_claude_today_  ? scr_claude_today_->screen()  : nullptr;
        case Mode::CLAUDE_MONTH: return scr_claude_month_ ? scr_claude_month_->screen() : nullptr;
        case Mode::OPENAI_TODAY: return scr_openai_today_ ? scr_openai_today_->screen() : nullptr;
        case Mode::OPENAI_MONTH: return scr_openai_month_? scr_openai_month_->screen(): nullptr;
        case Mode::COMBINED:     return scr_combined_     ? scr_combined_->screen()     : nullptr;
        case Mode::SETTINGS:     return scr_settings_     ? scr_settings_->screen()     : nullptr;
        default: return nullptr;
    }
}

IScreenProvider* UIManager::providerForMode(Mode m) {
    switch (m) {
        case Mode::CLAUDE_TODAY:  return scr_claude_today_;
        case Mode::CLAUDE_MONTH: return scr_claude_month_;
        case Mode::OPENAI_TODAY: return scr_openai_today_;
        case Mode::OPENAI_MONTH: return scr_openai_month_;
        case Mode::COMBINED:     return scr_combined_;
        default: return nullptr;
    }
}

// ── Mode switching ───────────────────────────────────────────────

void UIManager::transitionTo(int idx) {
    lv_obj_t* scr = screenForMode(modes_[idx]);
    if (!scr) return;
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN,
                        Anim::transition, 0, false);
}

void UIManager::nextMode() {
    if (num_modes_ < 2) return;
    cur_idx_ = (cur_idx_ + 1) % num_modes_;
    transitionTo(cur_idx_);

    IScreenProvider* sp = providerForMode(modes_[cur_idx_]);
    if (sp) sp->flashHero();
}

void UIManager::adjustTimeframe(int delta) {
    int tf = (int)timeframe_ + delta;
    int mx = (int)Timeframe::_COUNT;
    if (tf < 0) tf = mx - 1;
    if (tf >= mx) tf = 0;
    timeframe_ = (Timeframe)tf;
}

// ── Data push ────────────────────────────────────────────────────

void UIManager::updateData(const UsageSnapshot& claude,
                           const UsageSnapshot& openai,
                           SettingsStore& store)
{
    float cd = store.claudeDaily(),   cm = store.claudeMonthly();
    float od = store.openaiDaily(),   om = store.openaiMonthly();

    auto tfToday = timeframe_;
    auto tfMonth = timeframe_;

    if (scr_claude_today_)
        scr_claude_today_->update(claude, cd, cm, tfToday);
    if (scr_claude_month_) {
        // Month screen always shows D30
        UsageSnapshot monthSnap = claude;
        scr_claude_month_->update(monthSnap, cd, cm, Timeframe::D30);
    }
    if (scr_openai_today_)
        scr_openai_today_->update(openai, od, om, tfToday);
    if (scr_openai_month_) {
        UsageSnapshot monthSnap = openai;
        scr_openai_month_->update(monthSnap, od, om, Timeframe::D30);
    }
    if (scr_combined_) {
        UsageSnapshot combined;
        memset(&combined, 0, sizeof(combined));
        combined.spend_today  = claude.spend_today  + openai.spend_today;
        combined.spend_month  = claude.spend_month  + openai.spend_month;
        combined.tokens_today = claude.tokens_today + openai.tokens_today;
        combined.tokens_month = claude.tokens_month + openai.tokens_month;
        for (int i = 0; i < SPARK_POINTS; i++)
            combined.hourly_spend[i] = claude.hourly_spend[i] + openai.hourly_spend[i];
        combined.last_updated = (claude.last_updated < openai.last_updated)
                                ? claude.last_updated : openai.last_updated;
        combined.valid = claude.valid && openai.valid;
        scr_combined_->update(combined, cd + od, cm + om, tfToday);
    }

    if (scr_settings_) scr_settings_->update(store);
}

// ── Clock refresh (called every loop) ────────────────────────────

void UIManager::tick() {
    static uint32_t last_clock = 0;
    uint32_t now = millis();
    if (now - last_clock < 10000) return;  // every 10 s
    last_clock = now;

    if (scr_claude_today_)  scr_claude_today_->refreshClock();
    if (scr_claude_month_)  scr_claude_month_->refreshClock();
    if (scr_openai_today_)  scr_openai_today_->refreshClock();
    if (scr_openai_month_)  scr_openai_month_->refreshClock();
    if (scr_combined_)      scr_combined_->refreshClock();
    if (scr_settings_)      scr_settings_->refreshClock();
}

// ── Backlight / idle ─────────────────────────────────────────────

void UIManager::onActivity() {
    last_act_  = millis();
    bl_target_ = BL_FULL;
}

void UIManager::updateBacklight() {
    uint32_t idle = millis() - last_act_;
    if (idle > IDLE_DEEP_DIM_MS)      bl_target_ = BL_DEEP_DIM;
    else if (idle > IDLE_DIM_MS)       bl_target_ = BL_DIM;

    if (bl_current_ != bl_target_) {
        // Smooth ramp: move ~1/8 of the gap each frame
        int diff = (int)bl_target_ - (int)bl_current_;
        int step = diff / 8;
        if (step == 0) step = (diff > 0) ? 1 : -1;
        bl_current_ = (uint8_t)((int)bl_current_ + step);
        analogWrite(PIN_TFT_BL, bl_current_);
    }
}

// ── Boot screens (static factory methods) ────────────────────────

lv_obj_t* UIManager::makeSplash() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, Font::body(), 0);
    lv_obj_set_style_text_color(title, Color::claude(), 0);
    lv_obj_set_style_text_letter_space(title, 4, 0);
    lv_label_set_text(title, "TOKENJAR");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -10);

    // Thin progress bar under the title
    lv_obj_t* bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 120, 2);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 14);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, Color::hairline(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, Color::claude(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(bar, 1400, LV_PART_MAIN);
    lv_bar_set_value(bar, 100, LV_ANIM_ON);

    return scr;
}

lv_obj_t* UIManager::makeQRSetup() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    // Generate QR for captive portal URL
    QRCode qrcode;
    uint8_t qrcodeBytes[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrcodeBytes, 3, ECC_LOW, "HTTP://192.168.4.1");

    int scale = 4;
    int qr_px = qrcode.size * scale;  // 29*4 = 116

    // Draw QR directly into RGB565 buffer (no LVGL draw calls)
    static uint8_t* qr_buf = nullptr;
    if (!qr_buf) qr_buf = (uint8_t*)malloc(qr_px * qr_px * 2);

    if (qr_buf) {
        uint16_t* px = (uint16_t*)qr_buf;
        uint16_t white = lv_color_to_u16(Color::text());
        uint16_t black = lv_color_to_u16(Color::bg());

        // Fill white background
        for (int i = 0; i < qr_px * qr_px; i++) px[i] = white;

        // Draw dark modules
        for (int y = 0; y < (int)qrcode.size; y++) {
            for (int x = 0; x < (int)qrcode.size; x++) {
                if (qrcode_getModule(&qrcode, x, y)) {
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            px[(y * scale + dy) * qr_px + (x * scale + dx)] = black;
                        }
                    }
                }
            }
        }

        lv_obj_t* canvas = lv_canvas_create(scr);
        lv_canvas_set_buffer(canvas, qr_buf, qr_px, qr_px, LV_COLOR_FORMAT_RGB565);
        lv_obj_align(canvas, LV_ALIGN_CENTER, 0, -30);
    }

    // SSID text
    lv_obj_t* ssid = lv_label_create(scr);
    lv_obj_set_style_text_font(ssid, Font::label(), 0);
    lv_obj_set_style_text_color(ssid, Color::dim(), 0);
    lv_obj_set_style_text_letter_space(ssid, 2, 0);
    lv_label_set_text_fmt(ssid, "CONNECT TO: %s", AP_SSID);
    lv_obj_align(ssid, LV_ALIGN_BOTTOM_MID, 0, -50);

    lv_obj_t* hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, Font::small(), 0);
    lv_obj_set_style_text_color(hint, Color::vdim(), 0);
    lv_label_set_text(hint, "THEN OPEN 192.168.4.1");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -34);

    return scr;
}

lv_obj_t* UIManager::makeConnecting(const char* ssid) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* spinner = lv_spinner_create(scr);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(spinner, Color::dim(), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, Color::claude(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);

    lv_obj_t* lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl, Font::label(), 0);
    lv_obj_set_style_text_color(lbl, Color::dim(), 0);
    char buf[64];
    snprintf(buf, sizeof(buf), "CONNECTING TO %s", ssid);
    lv_label_set_text(lbl, buf);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 30);

    return scr;
}

lv_obj_t* UIManager::makeSyncing() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl, Font::label(), 0);
    lv_obj_set_style_text_color(lbl, Color::dim(), 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    lv_label_set_text(lbl, "SYNCING");
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return scr;
}

// ── Orientation choice screen (first boot) ───────────────────────
// Small static state so the caller can drive selection via encoder.
static lv_obj_t* s_orient_lbl_h = nullptr;
static lv_obj_t* s_orient_lbl_v = nullptr;
static bool      s_orient_sel_horizontal = true;

lv_obj_t* UIManager::makeOrientationChoice() {
    s_orient_sel_horizontal = true;

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, Font::label(), 0);
    lv_obj_set_style_text_color(title, Color::dim(), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_label_set_text(title, "CHOOSE ORIENTATION");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    // Horizontal option
    s_orient_lbl_h = lv_label_create(scr);
    lv_obj_set_style_text_font(s_orient_lbl_h, Font::body(), 0);
    lv_obj_set_style_text_letter_space(s_orient_lbl_h, 3, 0);
    lv_obj_align(s_orient_lbl_h, LV_ALIGN_CENTER, 0, -14);

    // Vertical option
    s_orient_lbl_v = lv_label_create(scr);
    lv_obj_set_style_text_font(s_orient_lbl_v, Font::body(), 0);
    lv_obj_set_style_text_letter_space(s_orient_lbl_v, 3, 0);
    lv_obj_align(s_orient_lbl_v, LV_ALIGN_CENTER, 0, 18);

    // Hint at bottom
    lv_obj_t* hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, Font::small(), 0);
    lv_obj_set_style_text_color(hint, Color::vdim(), 0);
    lv_label_set_text(hint, "TURN TO CHANGE  *  CLICK TO CONFIRM");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);

    // Render default selection (horizontal)
    orientationChoiceSetSel(true);
    return scr;
}

void UIManager::orientationChoiceSetSel(bool horizontal) {
    s_orient_sel_horizontal = horizontal;
    if (!s_orient_lbl_h || !s_orient_lbl_v) return;

    if (horizontal) {
        lv_label_set_text(s_orient_lbl_h, ">  HORIZONTAL  <");
        lv_label_set_text(s_orient_lbl_v, "   VERTICAL");
        lv_obj_set_style_text_color(s_orient_lbl_h, Color::claude(), 0);
        lv_obj_set_style_text_color(s_orient_lbl_v, Color::dim(),    0);
    } else {
        lv_label_set_text(s_orient_lbl_h, "   HORIZONTAL");
        lv_label_set_text(s_orient_lbl_v, ">  VERTICAL  <");
        lv_obj_set_style_text_color(s_orient_lbl_h, Color::dim(),    0);
        lv_obj_set_style_text_color(s_orient_lbl_v, Color::claude(), 0);
    }
    // Re-align since label widths change when arrows get added/removed
    lv_obj_align(s_orient_lbl_h, LV_ALIGN_CENTER, 0, -14);
    lv_obj_align(s_orient_lbl_v, LV_ALIGN_CENTER, 0,  18);
}

bool UIManager::orientationChoiceGetSel() {
    return s_orient_sel_horizontal;
}
