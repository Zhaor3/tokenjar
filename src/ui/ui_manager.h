#pragma once
#include <lvgl.h>
#include "api/usage_provider.h"
#include "api/claude_plan.h"
#include "api/codex_plan.h"

class SettingsStore;
class IScreenProvider;
class IScreenSettings;
class IScreenPlan;
class IScreenCodexPlan;

// Provider-level modes.  Button press cycles through these.
// Timeframe (Today / 30 Days / etc.) is controlled by encoder rotation
// within the CLAUDE and OPENAI modes — no separate day/month screens.
enum class Mode : uint8_t {
    CLAUDE,           // Admin API spend (timeframe via rotation)
    CLAUDE_PLAN,      // Pro/Max subscription usage (session key)
    OPENAI,           // Admin API spend (timeframe via rotation)
    CODEX_PLAN,       // OpenAI / ChatGPT session usage
    SETTINGS,
    _COUNT
};

enum class SettingsMenuSelection : uint8_t {
    MAIN_PAGE,
    API_PORTAL,
    RESET,
    CANCEL,
};

class UIManager {
    // One screen per provider (null if not configured).
    IScreenProvider* scr_claude_      = nullptr;
    IScreenPlan*     scr_claude_plan_ = nullptr;
    IScreenProvider* scr_openai_      = nullptr;
    IScreenCodexPlan* scr_codex_plan_ = nullptr;
    IScreenSettings* scr_settings_    = nullptr;

    bool horizontal_ = true;

    // Active mode list (populated from available API keys)
    static constexpr int MAX_MODES = 5;
    Mode     modes_[MAX_MODES];
    int      num_modes_  = 0;
    int      cur_idx_    = 0;
    Timeframe timeframe_ = Timeframe::D30;

    // Backlight
    uint8_t  bl_current_ = 0;
    uint8_t  bl_target_  = 0;
    uint32_t last_act_   = 0;

    void      ensureScreens(SettingsStore& store);
    void      rebuildModes(SettingsStore& store, Mode preferred);
    lv_obj_t* screenForMode(Mode m);
    void      transitionTo(int idx);
    IScreenProvider* providerForMode(Mode m);

public:
    void init(SettingsStore& store, bool horizontal);
    void refreshModes(SettingsStore& store);
    void nextMode();
    void adjustTimeframe(int delta);
    void setTimeframe(Timeframe tf);
    void onActivity();
    void updateBacklight();
    void updateData(const UsageSnapshot& claude,
                    const UsageSnapshot& openai,
                    SettingsStore& store);
    void updatePlan(const ClaudePlanSnapshot& plan);
    void updateCodexPlan(const CodexPlanSnapshot& plan);
    void tick();                // call every loop — refreshes clock

    // Boot screens (standalone, not in the mode ring).
    static lv_obj_t* makeSplash();
    static lv_obj_t* makeQRSetup();
    static lv_obj_t* makeConnecting(const char* ssid);
    static lv_obj_t* makeSyncing();
    static lv_obj_t* makeConfigPortal(const char* url);

    // First-boot orientation choice screen.
    static lv_obj_t* makeOrientationChoice();
    static void      orientationChoiceSetSel(bool horizontal);
    static bool      orientationChoiceGetSel();

    // Factory-reset confirmation (long-press from RUNNING).
    static lv_obj_t* makeResetConfirm();
    static void      resetConfirmSetSel(bool yes);
    static bool      resetConfirmGetSel();

    // Long-press action menu.
    static lv_obj_t* makeSettingsMenu();
    static void      settingsMenuMove(int delta);
    static SettingsMenuSelection settingsMenuGetSel();

    // Main page / timeframe picker.
    static lv_obj_t* makeMainPageMenu(Timeframe current);
    static void      mainPageMenuMove(int delta);
    static Timeframe mainPageMenuGetSel();

    Mode currentMode() const { return modes_[cur_idx_]; }
    Timeframe currentTimeframe() const { return timeframe_; }
    bool isHorizontal() const { return horizontal_; }
};
