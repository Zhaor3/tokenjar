#pragma once
#include <lvgl.h>
#include "api/usage_provider.h"

class SettingsStore;
class ScreenProvider;
class ScreenSettings;

enum class Mode : uint8_t {
    CLAUDE_TODAY,
    CLAUDE_MONTH,
    OPENAI_TODAY,
    OPENAI_MONTH,
    COMBINED,
    SETTINGS,
    _COUNT
};

class UIManager {
    // Screens (allocated on init; null if provider not configured)
    ScreenProvider*  scr_claude_today_  = nullptr;
    ScreenProvider*  scr_claude_month_  = nullptr;
    ScreenProvider*  scr_openai_today_  = nullptr;
    ScreenProvider*  scr_openai_month_  = nullptr;
    ScreenProvider*  scr_combined_      = nullptr;
    ScreenSettings*  scr_settings_      = nullptr;

    // Active mode list (populated from available API keys)
    static constexpr int MAX_MODES = 6;
    Mode     modes_[MAX_MODES];
    int      num_modes_  = 0;
    int      cur_idx_    = 0;
    Timeframe timeframe_ = Timeframe::H24;

    // Backlight
    uint8_t  bl_current_ = 0;
    uint8_t  bl_target_  = 0;
    uint32_t last_act_   = 0;

    lv_obj_t* screenForMode(Mode m);
    void      transitionTo(int idx);
    void      updateCurrentScreen(const UsageSnapshot& claude,
                                  const UsageSnapshot& openai,
                                  SettingsStore& store);
    ScreenProvider* providerForMode(Mode m);

public:
    void init(SettingsStore& store);
    void nextMode();
    void adjustTimeframe(int delta);
    void onActivity();
    void updateBacklight();
    void updateData(const UsageSnapshot& claude,
                    const UsageSnapshot& openai,
                    SettingsStore& store);
    void tick();                // call every loop — refreshes clock

    // Boot screens (standalone, not in the mode ring)
    static lv_obj_t* makeSplash();
    static lv_obj_t* makeQRSetup();
    static lv_obj_t* makeConnecting(const char* ssid);
    static lv_obj_t* makeSyncing();

    Mode currentMode() const { return modes_[cur_idx_]; }
};
