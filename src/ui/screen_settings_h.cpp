#include "ui/screen_settings_h.h"
#include "storage/settings_store.h"
#include "config.h"
#include "theme.h"
#include <WiFi.h>
#include <cstdio>
#include <ctime>

using namespace Theme;

// Two-column layout constants (320 × 240)
static constexpr int HSX         = 16;     // column 1 x
static constexpr int HSX_COL2    = 172;    // column 2 x
static constexpr int HSY_TITLE   = 16;
static constexpr int HSY_START   = 56;
static constexpr int HS_ROW_H    = 26;

static lv_obj_t* makeRow(lv_obj_t* parent, int x, int y, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, Font::label(), 0);
    lv_obj_set_style_text_color(lbl, Color::dim(), 0);
    lv_label_set_text(lbl, text);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

ScreenSettingsH::ScreenSettingsH() { buildLayout(); }

void ScreenSettingsH::buildLayout() {
    scr_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_, Color::bg(), 0);
    lv_obj_set_style_bg_opa(scr_, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr_, LV_SCROLLBAR_MODE_OFF);

    // Title
    lbl_title_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_title_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_title_, Color::dim(), 0);
    lv_obj_set_style_text_letter_space(lbl_title_, 2, 0);
    lv_label_set_text(lbl_title_, "SETTINGS");
    lv_obj_set_pos(lbl_title_, HSX, HSY_TITLE);

    // Time — top-right
    lbl_time_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_time_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_time_, Color::dim(), 0);
    lv_label_set_text(lbl_time_, "00:00");
    lv_obj_set_pos(lbl_time_, SCREEN_W_H - 48, HSY_TITLE);

    int y = HSY_START;
    // Row 1: WIFI | FREE HEAP
    lbl_wifi_    = makeRow(scr_, HSX,      y, "WIFI: ---");
    lbl_heap_    = makeRow(scr_, HSX_COL2, y, "FREE HEAP: ---");
    y += HS_ROW_H;

    // Row 2: IP | UPTIME
    lbl_ip_      = makeRow(scr_, HSX,      y, "IP: ---");
    lbl_uptime_  = makeRow(scr_, HSX_COL2, y, "UPTIME: ---");
    y += HS_ROW_H;

    // Row 3: LAST REFRESH (full width)
    lbl_refresh_ = makeRow(scr_, HSX, y, "LAST REFRESH: ---");
    y += HS_ROW_H + 16;

    // Version footer
    lbl_ver_ = makeRow(scr_, HSX, y, "TOKENJAR V1.0");
    lv_obj_set_style_text_color(lbl_ver_, Color::vdim(), 0);
}

void ScreenSettingsH::update(const SettingsStore& /*store*/) {
    char buf[64];

    // WiFi
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "WIFI: %s", WiFi.SSID().c_str());
    } else {
        snprintf(buf, sizeof(buf), "WIFI: DISCONNECTED");
    }
    lv_label_set_text(lbl_wifi_, buf);

    // IP
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
    } else {
        snprintf(buf, sizeof(buf), "IP: ---");
    }
    lv_label_set_text(lbl_ip_, buf);

    // Heap
    snprintf(buf, sizeof(buf), "FREE HEAP: %lu B", (unsigned long)ESP.getFreeHeap());
    lv_label_set_text(lbl_heap_, buf);

    // Uptime
    uint32_t sec = millis() / 1000;
    uint32_t h = sec / 3600;
    uint32_t m = (sec % 3600) / 60;
    snprintf(buf, sizeof(buf), "UPTIME: %luh %lum", (unsigned long)h, (unsigned long)m);
    lv_label_set_text(lbl_uptime_, buf);
}

void ScreenSettingsH::refreshClock() {
    struct tm t;
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    lv_label_set_text(lbl_time_, buf);
}
