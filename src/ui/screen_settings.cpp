#include "ui/screen_settings.h"
#include "storage/settings_store.h"
#include "config.h"
#include "theme.h"
#include <WiFi.h>
#include <cstdio>
#include <ctime>

using namespace Theme;

static constexpr int X = 16;
static constexpr int Y_START = 16;
static constexpr int ROW_H = 28;

static lv_obj_t* makeRow(lv_obj_t* parent, int y, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, Font::label(), 0);
    lv_obj_set_style_text_color(lbl, Color::dim(), 0);
    lv_label_set_text(lbl, text);
    lv_obj_set_pos(lbl, X, y);
    return lbl;
}

ScreenSettings::ScreenSettings() { buildLayout(); }

void ScreenSettings::buildLayout() {
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
    lv_obj_set_pos(lbl_title_, X, Y_START);

    // Time — top-right
    lbl_time_ = lv_label_create(scr_);
    lv_obj_set_style_text_font(lbl_time_, Font::label(), 0);
    lv_obj_set_style_text_color(lbl_time_, Color::dim(), 0);
    lv_label_set_text(lbl_time_, "00:00");
    lv_obj_set_pos(lbl_time_, SCREEN_W - 48, Y_START);

    int y = Y_START + ROW_H + 16;

    lbl_wifi_    = makeRow(scr_, y, "WIFI: ---");          y += ROW_H;
    lbl_ip_      = makeRow(scr_, y, "IP: ---");            y += ROW_H;
    lbl_heap_    = makeRow(scr_, y, "FREE HEAP: ---");     y += ROW_H;
    lbl_uptime_  = makeRow(scr_, y, "UPTIME: ---");        y += ROW_H;
    lbl_refresh_ = makeRow(scr_, y, "LAST REFRESH: ---");  y += ROW_H;

    y += 16;
    lbl_ver_ = makeRow(scr_, y, "TOKENJAR V1.0");
    lv_obj_set_style_text_color(lbl_ver_, Color::vdim(), 0);
}

void ScreenSettings::update(const SettingsStore& store) {
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

void ScreenSettings::refreshClock() {
    struct tm t;
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    lv_label_set_text(lbl_time_, buf);
}
