#pragma once
#include <lvgl.h>

class SettingsStore;

class ScreenSettings {
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
    ScreenSettings();
    void update(const SettingsStore& store);
    void refreshClock();
    lv_obj_t* screen() const { return scr_; }
};
