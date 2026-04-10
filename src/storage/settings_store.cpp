#include "storage/settings_store.h"
#include "config.h"

static const char* NS = "tokenjar";

void SettingsStore::begin() { prefs_.begin(NS, false); }

// ── WiFi ─────────────────────────────────────────────────────────
void   SettingsStore::setWiFi(const String& s, const String& p) { prefs_.putString("wifi_ssid", s); prefs_.putString("wifi_pass", p); }
String SettingsStore::ssid() { return prefs_.getString("wifi_ssid", ""); }
String SettingsStore::pass() { return prefs_.getString("wifi_pass", ""); }
bool   SettingsStore::hasWiFi() { return ssid().length() > 0; }

// ── API keys ─────────────────────────────────────────────────────
void   SettingsStore::setClaudeKey(const String& k) { prefs_.putString("claude_key", k); }
void   SettingsStore::setOpenAIKey(const String& k) { prefs_.putString("openai_key", k); }
String SettingsStore::claudeKey()  { return prefs_.getString("claude_key", ""); }
String SettingsStore::openaiKey()  { return prefs_.getString("openai_key", ""); }
bool   SettingsStore::hasClaude()  { return claudeKey().length() > 0; }
bool   SettingsStore::hasOpenAI()  { return openaiKey().length() > 0; }

// ── Budgets ──────────────────────────────────────────────────────
void SettingsStore::setClaudeBudget(float d, float m) { prefs_.putFloat("claude_d_bud", d); prefs_.putFloat("claude_m_bud", m); }
void SettingsStore::setOpenAIBudget(float d, float m) { prefs_.putFloat("openai_d_bud", d); prefs_.putFloat("openai_m_bud", m); }
float SettingsStore::claudeDaily()   { return prefs_.getFloat("claude_d_bud", DEFAULT_CLAUDE_DAILY_BUDGET); }
float SettingsStore::claudeMonthly() { return prefs_.getFloat("claude_m_bud", DEFAULT_CLAUDE_MONTHLY_BUDGET); }
float SettingsStore::openaiDaily()   { return prefs_.getFloat("openai_d_bud", DEFAULT_OPENAI_DAILY_BUDGET); }
float SettingsStore::openaiMonthly() { return prefs_.getFloat("openai_m_bud", DEFAULT_OPENAI_MONTHLY_BUDGET); }

// ── Timezone ─────────────────────────────────────────────────────
void   SettingsStore::setTimezone(const String& tz) { prefs_.putString("timezone", tz); }
String SettingsStore::timezone() { return prefs_.getString("timezone", DEFAULT_TZ); }

// ── OTA ──────────────────────────────────────────────────────────
void   SettingsStore::setOTAPass(const String& p) { prefs_.putString("ota_pass", p); }
String SettingsStore::otaPass()    { return prefs_.getString("ota_pass", ""); }
bool   SettingsStore::hasOTAPass() { return otaPass().length() > 0; }

// ── Snapshot cache ───────────────────────────────────────────────
void SettingsStore::saveCache(const char* prov, const UsageSnapshot& s) {
    char key[16];
    snprintf(key, sizeof(key), "%.10s_c", prov);
    prefs_.putBytes(key, &s, sizeof(s));
}

bool SettingsStore::loadCache(const char* prov, UsageSnapshot& s) {
    char key[16];
    snprintf(key, sizeof(key), "%.10s_c", prov);
    return prefs_.getBytes(key, &s, sizeof(s)) == sizeof(s);
}

void SettingsStore::clear() { prefs_.clear(); }
