#pragma once
#include <lvgl.h>
#include "api/usage_provider.h"
#include "api/claude_plan.h"

class SettingsStore;
class IScreenProvider;
class IScreenSettings;
class IScreenPlan;

enum class Mode : uint8_t {
    CLAUDE_TODAY,
    CLAUDE_MONTH,
    CLAUDE_PLAN,      // Pro/Max subscription usage (requires session key)
    OPENAI_TODAY,
    OPENAI_MONTH,
    COMBINED,
    SETTINGS,
    _COUNT
};

class UIManager {
    // Screens (allocated on init; null if provider not configured).
    // Concrete type depends on orientation picked at init(); we store
    // pointers through the orientation-agnostic interface.
    IScreenProvider* scr_claude_today_  = nullptr;
    IScreenProvider* scr_claude_month_  = nullptr;
    IScreenPlan*     scr_claude_plan_   = nullptr;
    IScreenProvider* scr_openai_today_  = nullptr;
    IScreenProvider* scr_openai_month_  = nullptr;
    IScreenProvider* scr_combined_      = nullptr;
    IScreenSettings* scr_settings_      = nullptr;

    bool horizontal_ = true;

    // Active mode list (populated from available API keys)
    static constexpr int MAX_MODES = 7;
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
    IScreenProvider* providerForMode(Mode m);

public:
    void init(SettingsStore& store, bool horizontal);
    void nextMode();
    void adjustTimeframe(int delta);
    void onActivity();
    void updateBacklight();
    void updateData(const UsageSnapshot& claude,
                    const UsageSnapshot& openai,
                    SettingsStore& store);
    void updatePlan(const ClaudePlanSnapshot& plan);
    void tick();                // call every loop — refreshes clock

    // Boot screens (standalone, not in the mode ring).
    // These use LV_ALIGN_CENTER / LV_ALIGN_BOTTOM_MID so they work
    // in either orientation without additional parameters.
    static lv_obj_t* makeSplash();
    static lv_obj_t* makeQRSetup();
    static lv_obj_t* makeConnecting(const char* ssid);
    static lv_obj_t* makeSyncing();

    // First-boot orientation choice screen.
    // The caller drives it: turn encoder → orientationChoiceSetSel(!current),
    // press encoder → read orientationChoiceGetSel() and save.
    static lv_obj_t* makeOrientationChoice();
    static void      orientationChoiceSetSel(bool horizontal);
    static bool      orientationChoiceGetSel();

    Mode currentMode() const { return modes_[cur_idx_]; }
    bool isHorizontal() const { return horizontal_; }
};
