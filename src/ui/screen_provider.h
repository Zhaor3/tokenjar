#pragma once
#include <lvgl.h>
#include "api/usage_provider.h"

class ScreenProvider {
    lv_obj_t* scr_         = nullptr;
    lv_obj_t* lbl_name_    = nullptr;
    lv_obj_t* lbl_time_    = nullptr;
    lv_obj_t* dot_status_  = nullptr;
    lv_obj_t* lbl_frame_   = nullptr;
    lv_obj_t* lbl_hero_    = nullptr;
    lv_obj_t* line_div_    = nullptr;
    lv_obj_t* lbl_budget_  = nullptr;
    lv_obj_t* bar_budget_  = nullptr;
    lv_obj_t* lbl_tokens_  = nullptr;
    lv_obj_t* canvas_spark_= nullptr;
    lv_obj_t* lbl_footer_  = nullptr;

    lv_color_t accent_;
    const char* provider_name_;
    float       hero_val_ = 0;
    uint8_t*    spark_buf_ = nullptr;

    void buildLayout();
public:
    ScreenProvider(const char* name, lv_color_t accent);
    ~ScreenProvider();

    void update(const UsageSnapshot& snap,
                float daily_budget, float monthly_budget,
                Timeframe tf);
    void refreshClock();
    void setStatusDot(lv_color_t color);
    void drawSparkline(const float* data, int count, int segments);
    void flashHero();                       // press feedback
    void animateSparkline(const float* data, int count);

    lv_obj_t* screen() const { return scr_; }
};
