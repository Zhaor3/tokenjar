#include "ui/ui_manager.h"
#include "ui/screen_provider.h"
#include "ui/screen_provider_h.h"
#include "ui/screen_settings.h"
#include "ui/screen_settings_h.h"
#include "ui/screen_plan.h"
#include "ui/screen_plan_h.h"
#include "ui/i_screen_plan.h"
#include "ui/screen_codex_plan.h"
#include "ui/i_screen_codex_plan.h"
#include "storage/settings_store.h"
#include "config.h"
#include "theme.h"
#include <qrcode.h>
#include <Arduino.h>

using namespace Theme;

// ── Initialization ───────────────────────────────────────────────

static IScreenProvider* makeProviderScreen(bool horizontal, const char* name, lv_color_t accent) {
    if (horizontal) return new ScreenProviderH(name, accent);
    return new ScreenProvider(name, accent);
}

static IScreenPlan* makePlanScreen(bool horizontal) {
    if (horizontal) return new ScreenPlanH();
    return new ScreenPlan();
}

static IScreenCodexPlan* makeCodexPlanScreen(bool horizontal) {
    return new ScreenCodexPlan(horizontal);
}

static const char* modeName(Mode m) {
    switch (m) {
        case Mode::CLAUDE:      return "CLAUDE";
        case Mode::CLAUDE_PLAN: return "CLAUDE_PLAN";
        case Mode::OPENAI:      return "OPENAI";
        case Mode::CODEX_PLAN:  return "OPENAI_SESSION";
        case Mode::SETTINGS:    return "SETTINGS";
        default:                return "?";
    }
}

void UIManager::init(SettingsStore& store, bool horizontal) {
    horizontal_ = horizontal;
    ensureScreens(store);
    rebuildModes(store, Mode::_COUNT);

    bl_target_ = BL_FULL;
    bl_current_= BL_FULL;
    last_act_  = millis();
    analogWrite(PIN_TFT_BL, BL_FULL);

    if (num_modes_ > 0) {
        lv_obj_t* first = screenForMode(modes_[cur_idx_]);
        if (first) lv_screen_load(first);
    }
}

void UIManager::ensureScreens(SettingsStore& store) {
    // Build screens lazily so a user can add providers later through the
    // runtime settings portal without rebooting or factory-resetting.
    if (store.hasClaude() && !scr_claude_) {
        scr_claude_ = makeProviderScreen(horizontal_, "CLAUDE", Color::claude());
    }
    if (store.hasClaudeSession() && !scr_claude_plan_) {
        scr_claude_plan_ = makePlanScreen(horizontal_);
    }
    if (store.hasOpenAI() && !scr_openai_) {
        scr_openai_ = makeProviderScreen(horizontal_, "OPENAI", Color::openai());
    }
    if ((store.hasCodex() || store.hasOpenAI()) && !scr_codex_plan_) {
        scr_codex_plan_ = makeCodexPlanScreen(horizontal_);
    }
    if (!scr_settings_) {
        scr_settings_ = horizontal_
            ? static_cast<IScreenSettings*>(new ScreenSettingsH())
            : static_cast<IScreenSettings*>(new ScreenSettings());
    }
}

void UIManager::rebuildModes(SettingsStore& store, Mode preferred) {
    num_modes_ = 0;

    if (store.hasClaude())        modes_[num_modes_++] = Mode::CLAUDE;
    if (store.hasClaudeSession()) modes_[num_modes_++] = Mode::CLAUDE_PLAN;
    if (store.hasOpenAI())        modes_[num_modes_++] = Mode::OPENAI;
    if (store.hasCodex() || store.hasOpenAI()) modes_[num_modes_++] = Mode::CODEX_PLAN;
    modes_[num_modes_++] = Mode::SETTINGS;

    cur_idx_ = 0;
    for (int i = 0; i < num_modes_; ++i) {
        if (modes_[i] == preferred) {
            cur_idx_ = i;
            break;
        }
    }

    Serial.print("[UI] Modes:");
    for (int i = 0; i < num_modes_; ++i) {
        Serial.print(i == 0 ? " " : ", ");
        Serial.print(modeName(modes_[i]));
    }
    Serial.println();
}

void UIManager::refreshModes(SettingsStore& store) {
    Mode old = currentMode();
    ensureScreens(store);
    rebuildModes(store, old);
    transitionTo(cur_idx_);
}

// ── Screen lookup ────────────────────────────────────────────────

lv_obj_t* UIManager::screenForMode(Mode m) {
    switch (m) {
        case Mode::CLAUDE:      return scr_claude_      ? scr_claude_->screen()      : nullptr;
        case Mode::CLAUDE_PLAN: return scr_claude_plan_ ? scr_claude_plan_->screen() : nullptr;
        case Mode::OPENAI:      return scr_openai_      ? scr_openai_->screen()      : nullptr;
        case Mode::CODEX_PLAN:  return scr_codex_plan_  ? scr_codex_plan_->screen()  : nullptr;
        case Mode::SETTINGS:    return scr_settings_    ? scr_settings_->screen()    : nullptr;
        default: return nullptr;
    }
}

IScreenProvider* UIManager::providerForMode(Mode m) {
    switch (m) {
        case Mode::CLAUDE:  return scr_claude_;
        case Mode::OPENAI:  return scr_openai_;
        // CLAUDE_PLAN is not an IScreenProvider — returns null
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

void UIManager::setTimeframe(Timeframe tf) {
    if (tf >= Timeframe::_COUNT) return;
    timeframe_ = tf;
}

// ── Data push ────────────────────────────────────────────────────

void UIManager::updateData(const UsageSnapshot& claude,
                           const UsageSnapshot& openai,
                           SettingsStore& store)
{
    float cd = store.claudeDaily(),   cm = store.claudeMonthly();
    float od = store.openaiDaily(),   om = store.openaiMonthly();

    if (scr_claude_)
        scr_claude_->update(claude, cd, cm, timeframe_);
    if (scr_openai_)
        scr_openai_->update(openai, od, om, timeframe_);
    if (scr_settings_)
        scr_settings_->update(store);
}

// ── Plan data push (independent 5-min cadence) ───────────────────

void UIManager::updatePlan(const ClaudePlanSnapshot& plan) {
    if (scr_claude_plan_) scr_claude_plan_->update(plan);
}

void UIManager::updateCodexPlan(const CodexPlanSnapshot& plan) {
    if (scr_codex_plan_) scr_codex_plan_->update(plan);
}

// ── Clock refresh (called every loop) ────────────────────────────

void UIManager::tick() {
    static uint32_t last_clock = 0;
    uint32_t now = millis();
    if (now - last_clock < 10000) return;  // every 10 s
    last_clock = now;

    if (scr_claude_)      scr_claude_->refreshClock();
    if (scr_claude_plan_) scr_claude_plan_->refreshClock();
    if (scr_openai_)      scr_openai_->refreshClock();
    if (scr_codex_plan_)  scr_codex_plan_->refreshClock();
    if (scr_settings_)    scr_settings_->refreshClock();
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

lv_obj_t* UIManager::makeConfigPortal(const char* url) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, Font::label(), 0);
    lv_obj_set_style_text_color(title, Color::openai(), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_label_set_text(title, "API PORTAL");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

    lv_obj_t* line1 = lv_label_create(scr);
    lv_obj_set_style_text_font(line1, Font::label(), 0);
    lv_obj_set_style_text_color(line1, Color::dim(), 0);
    lv_label_set_text(line1, "OPEN IN BROWSER");
    lv_obj_align(line1, LV_ALIGN_CENTER, 0, -8);

    lv_obj_t* addr = lv_label_create(scr);
    lv_obj_set_style_text_font(addr, Font::body(), 0);
    lv_obj_set_style_text_color(addr, Color::text(), 0);
    lv_label_set_text(addr, url && *url ? url : "tokenjar.local");
    lv_obj_align(addr, LV_ALIGN_CENTER, 0, 18);

    lv_obj_t* hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, Font::small(), 0);
    lv_obj_set_style_text_color(hint, Color::vdim(), 0);
    lv_label_set_text(hint, "LONG-PRESS TO CLOSE");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);

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

// ── Reset confirmation screen ────────────────────────────────────
static lv_obj_t* s_reset_lbl_no  = nullptr;
static lv_obj_t* s_reset_lbl_yes = nullptr;
static bool      s_reset_sel_yes = false;   // default = NO (safer)

lv_obj_t* UIManager::makeResetConfirm() {
    s_reset_sel_yes = false;

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, Font::label(), 0);
    lv_obj_set_style_text_color(title, Color::claude(), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_label_set_text(title, "FACTORY RESET?");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t* sub = lv_label_create(scr);
    lv_obj_set_style_text_font(sub, Font::small(), 0);
    lv_obj_set_style_text_color(sub, Color::dim(), 0);
    lv_obj_set_style_text_letter_space(sub, 1, 0);
    lv_label_set_text(sub, "ERASES WIFI & API KEYS");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 46);

    s_reset_lbl_no = lv_label_create(scr);
    lv_obj_set_style_text_font(s_reset_lbl_no, Font::body(), 0);
    lv_obj_set_style_text_letter_space(s_reset_lbl_no, 3, 0);
    lv_obj_align(s_reset_lbl_no, LV_ALIGN_CENTER, 0, -8);

    s_reset_lbl_yes = lv_label_create(scr);
    lv_obj_set_style_text_font(s_reset_lbl_yes, Font::body(), 0);
    lv_obj_set_style_text_letter_space(s_reset_lbl_yes, 3, 0);
    lv_obj_align(s_reset_lbl_yes, LV_ALIGN_CENTER, 0, 22);

    lv_obj_t* hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, Font::small(), 0);
    lv_obj_set_style_text_color(hint, Color::vdim(), 0);
    lv_label_set_text(hint, "TURN TO CHANGE  *  CLICK TO CONFIRM");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);

    resetConfirmSetSel(false);
    return scr;
}

void UIManager::resetConfirmSetSel(bool yes) {
    s_reset_sel_yes = yes;
    if (!s_reset_lbl_no || !s_reset_lbl_yes) return;

    if (yes) {
        lv_label_set_text(s_reset_lbl_no,  "   NO");
        lv_label_set_text(s_reset_lbl_yes, ">  YES, RESET  <");
        lv_obj_set_style_text_color(s_reset_lbl_no,  Color::dim(),    0);
        lv_obj_set_style_text_color(s_reset_lbl_yes, Color::claude(), 0);
    } else {
        lv_label_set_text(s_reset_lbl_no,  ">  NO  <");
        lv_label_set_text(s_reset_lbl_yes, "   YES, RESET");
        lv_obj_set_style_text_color(s_reset_lbl_no,  Color::claude(), 0);
        lv_obj_set_style_text_color(s_reset_lbl_yes, Color::dim(),    0);
    }
    lv_obj_align(s_reset_lbl_no,  LV_ALIGN_CENTER, 0, -8);
    lv_obj_align(s_reset_lbl_yes, LV_ALIGN_CENTER, 0, 22);
}

bool UIManager::resetConfirmGetSel() {
    return s_reset_sel_yes;
}

// ── Long-press action menu ───────────────────────────────────────
static lv_obj_t* s_settings_lbl_main   = nullptr;
static lv_obj_t* s_settings_lbl_api    = nullptr;
static lv_obj_t* s_settings_lbl_reset  = nullptr;
static lv_obj_t* s_settings_lbl_cancel = nullptr;
static SettingsMenuSelection s_settings_sel = SettingsMenuSelection::MAIN_PAGE;

static void renderSettingsMenu() {
    if (!s_settings_lbl_main || !s_settings_lbl_api ||
        !s_settings_lbl_reset || !s_settings_lbl_cancel) return;

    auto paint = [](lv_obj_t* lbl, bool selected, const char* text) {
        char buf[32];
        snprintf(buf, sizeof(buf), selected ? ">  %s  <" : "   %s", text);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, selected ? Color::openai() : Color::dim(), 0);
    };

    paint(s_settings_lbl_main,
          s_settings_sel == SettingsMenuSelection::MAIN_PAGE,
          "MAIN PAGE");
    paint(s_settings_lbl_api,
          s_settings_sel == SettingsMenuSelection::API_PORTAL,
          "CHANGE API KEY");
    paint(s_settings_lbl_reset,
          s_settings_sel == SettingsMenuSelection::RESET,
          "RESET");
    paint(s_settings_lbl_cancel,
          s_settings_sel == SettingsMenuSelection::CANCEL,
          "CANCEL");

    lv_obj_align(s_settings_lbl_main,   LV_ALIGN_CENTER, 0, -42);
    lv_obj_align(s_settings_lbl_api,    LV_ALIGN_CENTER, 0, -14);
    lv_obj_align(s_settings_lbl_reset,  LV_ALIGN_CENTER, 0,  14);
    lv_obj_align(s_settings_lbl_cancel, LV_ALIGN_CENTER, 0,  42);
}

lv_obj_t* UIManager::makeSettingsMenu() {
    s_settings_sel = SettingsMenuSelection::MAIN_PAGE;

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, Font::label(), 0);
    lv_obj_set_style_text_color(title, Color::openai(), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_label_set_text(title, "MENU");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    s_settings_lbl_main = lv_label_create(scr);
    lv_obj_set_style_text_font(s_settings_lbl_main, Font::body(), 0);
    lv_obj_set_style_text_letter_space(s_settings_lbl_main, 3, 0);

    s_settings_lbl_api = lv_label_create(scr);
    lv_obj_set_style_text_font(s_settings_lbl_api, Font::body(), 0);
    lv_obj_set_style_text_letter_space(s_settings_lbl_api, 3, 0);

    s_settings_lbl_reset = lv_label_create(scr);
    lv_obj_set_style_text_font(s_settings_lbl_reset, Font::body(), 0);
    lv_obj_set_style_text_letter_space(s_settings_lbl_reset, 3, 0);

    s_settings_lbl_cancel = lv_label_create(scr);
    lv_obj_set_style_text_font(s_settings_lbl_cancel, Font::body(), 0);
    lv_obj_set_style_text_letter_space(s_settings_lbl_cancel, 3, 0);

    lv_obj_t* hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, Font::small(), 0);
    lv_obj_set_style_text_color(hint, Color::vdim(), 0);
    lv_label_set_text(hint, "TURN TO CHANGE  *  CLICK TO CONFIRM");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);

    renderSettingsMenu();
    return scr;
}

void UIManager::settingsMenuMove(int delta) {
    int idx = 0;
    switch (s_settings_sel) {
        case SettingsMenuSelection::MAIN_PAGE:  idx = 0; break;
        case SettingsMenuSelection::API_PORTAL: idx = 1; break;
        case SettingsMenuSelection::RESET:      idx = 2; break;
        case SettingsMenuSelection::CANCEL:     idx = 3; break;
    }

    idx += (delta > 0) ? 1 : -1;
    if (idx < 0) idx = 3;
    if (idx > 3) idx = 0;

    s_settings_sel = (idx == 0) ? SettingsMenuSelection::MAIN_PAGE :
                     (idx == 1) ? SettingsMenuSelection::API_PORTAL :
                     (idx == 2) ? SettingsMenuSelection::RESET :
                                  SettingsMenuSelection::CANCEL;
    renderSettingsMenu();
}

SettingsMenuSelection UIManager::settingsMenuGetSel() {
    return s_settings_sel;
}

// ── Main page / timeframe picker ─────────────────────────────────
static lv_obj_t* s_main_lbls[(int)Timeframe::_COUNT] = {};
static Timeframe s_main_sel = Timeframe::D30;

static void renderMainPageMenu() {
    for (int i = 0; i < (int)Timeframe::_COUNT; ++i) {
        lv_obj_t* lbl = s_main_lbls[i];
        if (!lbl) continue;
        bool selected = (i == (int)s_main_sel);
        char buf[28];
        snprintf(buf, sizeof(buf), selected ? ">  %s  <" : "   %s",
                 timeframeLabel((Timeframe)i));
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, selected ? Color::openai() : Color::dim(), 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -54 + i * 27);
    }
}

lv_obj_t* UIManager::makeMainPageMenu(Timeframe current) {
    s_main_sel = (current < Timeframe::_COUNT) ? current : Timeframe::D30;

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, Font::label(), 0);
    lv_obj_set_style_text_color(title, Color::openai(), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_label_set_text(title, "MAIN PAGE");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    for (int i = 0; i < (int)Timeframe::_COUNT; ++i) {
        s_main_lbls[i] = lv_label_create(scr);
        lv_obj_set_style_text_font(s_main_lbls[i], Font::body(), 0);
        lv_obj_set_style_text_letter_space(s_main_lbls[i], 3, 0);
    }

    lv_obj_t* hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, Font::small(), 0);
    lv_obj_set_style_text_color(hint, Color::vdim(), 0);
    lv_label_set_text(hint, "TURN TO CHANGE  *  CLICK TO CONFIRM");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);

    renderMainPageMenu();
    return scr;
}

void UIManager::mainPageMenuMove(int delta) {
    int idx = (int)s_main_sel + ((delta > 0) ? 1 : -1);
    int mx = (int)Timeframe::_COUNT;
    if (idx < 0) idx = mx - 1;
    if (idx >= mx) idx = 0;
    s_main_sel = (Timeframe)idx;
    renderMainPageMenu();
}

Timeframe UIManager::mainPageMenuGetSel() {
    return s_main_sel;
}
