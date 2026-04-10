#pragma once
#include <cstdint>

class Encoder {
    int64_t  last_count_ = 0;
    bool     btn_down_   = false;
    uint32_t btn_time_   = 0;
    bool     short_flag_ = false;
    bool     long_flag_  = false;
    bool     long_fired_ = false;
    uint32_t last_act_   = 0;
    int      rotation_   = 0;

public:
    void begin();
    void update();           // call every loop iteration

    int  rotation();         // consume accumulated delta (-N / 0 / +N)
    bool wasPressed();       // consume short-press event
    bool wasLongPressed();   // consume long-press event
    bool isActive() const;   // any activity in the last 500 ms
    uint32_t lastActivity() const { return last_act_; }
};
