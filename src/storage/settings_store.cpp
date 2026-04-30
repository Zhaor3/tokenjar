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

// ── Codex / ChatGPT OAuth session ────────────────────────────────
void   SettingsStore::setCodexAccessToken(const String& k) { prefs_.putString("codex_acc", k); }
void   SettingsStore::setCodexRefreshToken(const String& k) { prefs_.putString("codex_ref", k); }
void   SettingsStore::setCodexAccountId(const String& id) { prefs_.putString("codex_acct", id); }
String SettingsStore::codexAccessToken()  { return prefs_.getString("codex_acc", ""); }
String SettingsStore::codexRefreshToken() { return prefs_.getString("codex_ref", ""); }
String SettingsStore::codexAccountId()    { return prefs_.getString("codex_acct", ""); }
bool   SettingsStore::hasCodex() {
    return codexAccessToken().length() > 0 || codexRefreshToken().length() > 0;
}

// ── Claude.ai session cookie (plan tracking) ─────────────────────
// Separate from the Admin API key. When this is set, TokenJar scrapes
// the claude.ai subscription usage page instead of the Admin API for
// the "CLAUDE PLAN" screen.
void   SettingsStore::setClaudeSession(const String& k) {
    // Changing the session key invalidates the cached org id.
    String prev = prefs_.getString("claude_sid", "");
    if (prev != k) prefs_.remove("claude_org");
    prefs_.putString("claude_sid", k);
}
String SettingsStore::claudeSession()    { return prefs_.getString("claude_sid", ""); }
bool   SettingsStore::hasClaudeSession() { return claudeSession().length() > 0; }

void   SettingsStore::setClaudeOrgId(const String& uuid) { prefs_.putString("claude_org", uuid); }
String SettingsStore::claudeOrgId()    { return prefs_.getString("claude_org", ""); }
void   SettingsStore::clearClaudeOrgId() { prefs_.remove("claude_org"); }

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

// ── Main API usage timeframe ─────────────────────────────────────
void SettingsStore::setTimeframe(Timeframe tf) {
    if (tf >= Timeframe::_COUNT) return;
    prefs_.putUChar("timeframe", (uint8_t)tf);
}

Timeframe SettingsStore::timeframe() {
    uint8_t tf = prefs_.getUChar("timeframe", (uint8_t)Timeframe::D30);
    if (tf >= (uint8_t)Timeframe::_COUNT) return Timeframe::D30;
    return (Timeframe)tf;
}

// ── OTA ──────────────────────────────────────────────────────────
void   SettingsStore::setOTAPass(const String& p) { prefs_.putString("ota_pass", p); }
String SettingsStore::otaPass()    { return prefs_.getString("ota_pass", ""); }
bool   SettingsStore::hasOTAPass() { return otaPass().length() > 0; }

// ── Orientation ──────────────────────────────────────────────────
void   SettingsStore::setOrientation(const String& o) { prefs_.putString("orient", o); }
String SettingsStore::orientation()      { return prefs_.getString("orient", ""); }
bool   SettingsStore::hasOrientation()   { return orientation().length() > 0; }
bool   SettingsStore::orientationHorizontal() {
    // Any stored value other than "vertical" is treated as horizontal (the default).
    return orientation() != "vertical";
}

// ── Default timeframe ────────────────────────────────────────────
void SettingsStore::setDefaultTimeframe(Timeframe t) {
    prefs_.putUChar("def_tf", (uint8_t)t);
}

Timeframe SettingsStore::defaultTimeframe() {
    uint8_t v = prefs_.getUChar("def_tf", (uint8_t)Timeframe::H24);
    if (v >= (uint8_t)Timeframe::_COUNT) v = (uint8_t)Timeframe::H24;
    return (Timeframe)v;
}

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

// Claude plan snapshot — separate key, different shape than UsageSnapshot.
void SettingsStore::savePlanCache(const ClaudePlanSnapshot& s) {
    prefs_.putBytes("plan_c", &s, sizeof(s));
}

bool SettingsStore::loadPlanCache(ClaudePlanSnapshot& s) {
    return prefs_.getBytes("plan_c", &s, sizeof(s)) == sizeof(s);
}

void SettingsStore::saveCodexCache(const CodexPlanSnapshot& s) {
    prefs_.putBytes("codex_c", &s, sizeof(s));
}

bool SettingsStore::loadCodexCache(CodexPlanSnapshot& s) {
    return prefs_.getBytes("codex_c", &s, sizeof(s)) == sizeof(s);
}

void SettingsStore::clear() { prefs_.clear(); }
