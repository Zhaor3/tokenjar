#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "api/usage_provider.h"
#include "api/claude_plan.h"
#include "api/codex_plan.h"

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

    // Codex / ChatGPT session OAuth tokens — used for Codex subscription
    // rate-limit tracking via ChatGPT backend. Separate from OpenAI Admin API.
    void     setCodexAccessToken(const String& k);
    void     setCodexRefreshToken(const String& k);
    void     setCodexAccountId(const String& id);
    String   codexAccessToken();
    String   codexRefreshToken();
    String   codexAccountId();
    bool     hasCodex();

    // Claude.ai session (sk-ant-sid01-...) — used to scrape subscription
    // plan usage (session %, weekly %, extra-usage $) from claude.ai's
    // undocumented endpoints. Completely separate from the Admin API key.
    void     setClaudeSession(const String& k);
    String   claudeSession();
    bool     hasClaudeSession();

    // Org UUID cache — fetched once on first successful plan request,
    // then reused until the session key changes.
    void     setClaudeOrgId(const String& uuid);
    String   claudeOrgId();
    void     clearClaudeOrgId();

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

    // Main API usage timeframe
    void      setTimeframe(Timeframe tf);
    Timeframe timeframe();

    // OTA
    void   setOTAPass(const String& p);
    String otaPass();
    bool   hasOTAPass();

    // Display orientation ("horizontal" | "vertical")
    void   setOrientation(const String& o);
    String orientation();
    bool   hasOrientation();
    bool   orientationHorizontal();  // true if horizontal (default)

    // Default timeframe shown on provider screens at boot.
    // Stored as the underlying uint8_t of Timeframe; defaults to H24 (TODAY).
    void      setDefaultTimeframe(Timeframe t);
    Timeframe defaultTimeframe();

    // Cached snapshots (survive reboot)
    void saveCache(const char* provider, const UsageSnapshot& s);
    bool loadCache(const char* provider, UsageSnapshot& s);

    // Cached Claude plan snapshot (separate key — different shape)
    void savePlanCache(const ClaudePlanSnapshot& s);
    bool loadPlanCache(ClaudePlanSnapshot& s);

    // Cached Codex plan snapshot.
    void saveCodexCache(const CodexPlanSnapshot& s);
    bool loadCodexCache(CodexPlanSnapshot& s);

    // Factory reset
    void clear();
};
