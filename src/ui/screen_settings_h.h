#pragma once
#include <lvgl.h>
#include "ui/i_screen_settings.h"

class SettingsStore;

// Horizontal (landscape, 320×240) settings screen.
// Two-column layout makes good use of the extra width.
class ScreenSettingsH : public IScreenSettings {
    lv_obj_t* scr_         = nullptr;
    lv_obj_t* lbl_title_   = nullptr;
    lv_obj_t* lbl_wifi_    = nullptr;
    lv_obj_t* lbl_ip_      = nullptr;
    lv_obj_t* lbl_heap_    = nullptr;
    lv_obj_t* lbl_uptime_  = nullptr;
    lv_obj_t* lbl_refresh_ = nullptr;
    lv_obj_t* lbl_ver_     = nullptr;
    lv_obj_t* lbl_time_    = nullptr;

    void buildLayout();

public:
    ScreenSettingsH();
    ~ScreenSettingsH() override = default;
    void update(const SettingsStore& store) override;
    void refreshClock() override;
    lv_obj_t* screen() const override { return scr_; }
};
