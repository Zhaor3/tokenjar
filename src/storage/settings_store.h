#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "api/usage_provider.h"

class SettingsStore {
    Preferences prefs_;
public:
    void begin();

    // WiFi
    void     setWiFi(const String& ssid, const String& pass);
    String   ssid();
    String   pass();
    bool     hasWiFi();

    // API keys
    void     setClaudeKey(const String& k);
    void     setOpenAIKey(const String& k);
    String   claudeKey();
    String   openaiKey();
    bool     hasClaude();
    bool     hasOpenAI();

    // Budgets
    void  setClaudeBudget(float daily, float monthly);
    void  setOpenAIBudget(float daily, float monthly);
    float claudeDaily();
    float claudeMonthly();
    float openaiDaily();
    float openaiMonthly();

    // Timezone
    void   setTimezone(const String& tz);
    String timezone();

    // OTA
    void   setOTAPass(const String& p);
    String otaPass();
    bool   hasOTAPass();

    // Cached snapshots (survive reboot)
    void saveCache(const char* provider, const UsageSnapshot& s);
    bool loadCache(const char* provider, UsageSnapshot& s);

    // Factory reset
    void clear();
};
