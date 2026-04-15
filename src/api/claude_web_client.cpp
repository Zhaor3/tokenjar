#include "api/claude_web_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

// ─────────────────────────────────────────────────────────────────
// All of this is scraping an undocumented API. Expect to revisit if
// Anthropic changes any of:
//   - the cookie name (sessionKey)
//   - the endpoint paths (/api/organizations, /api/organizations/{id}/usage)
//   - the JSON shape (five_hour.utilization etc.)
//
// The header set mirrors what f-is-h/Usage4Claude and
// jonis100/claude-quota-tracker send — a Chrome-131 UA plus the full
// sec-fetch-* set, which has the best record against the Cloudflare
// bot-management layer in front of claude.ai.
// ─────────────────────────────────────────────────────────────────

static const char* UA =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/131.0.0.0 Safari/537.36";

// ── Plain-literal helper to read a JSON number/string into a float ──
// claude.ai has historically returned utilization as int, float, or
// stringified number — so we normalize.
static float numOrStr(JsonVariantConst v) {
    if (v.isNull()) return 0.0f;
    if (v.is<float>() || v.is<double>()) return v.as<float>();
    if (v.is<int>())                     return (float)v.as<int>();
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        if (!s) return 0.0f;
        return (float)atof(s);
    }
    return 0.0f;
}

// ── ISO 8601 UTC → epoch seconds (ESP32 has no timegm) ───────────
time_t ClaudeWebClient::parseIso8601Utc(const char* iso) {
    if (!iso || !*iso) return 0;
    int y, mo, d, h, mi, s;
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) {
        return 0;
    }
    struct tm tm = {};
    tm.tm_year  = y - 1900;
    tm.tm_mon   = mo - 1;
    tm.tm_mday  = d;
    tm.tm_hour  = h;
    tm.tm_min   = mi;
    tm.tm_sec   = s;
    tm.tm_isdst = 0;

    // mktime() interprets the struct tm as *local* time. We want UTC.
    // Temporarily flip TZ to UTC0, convert, then restore.
    const char* prev = getenv("TZ");
    String saved = prev ? String(prev) : "";
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(&tm);
    if (saved.length()) setenv("TZ", saved.c_str(), 1);
    else                unsetenv("TZ");
    tzset();
    return t;
}

// ── HTTP helper — all requests go through here ──────────────────
int ClaudeWebClient::getWithCookie(const char* url,
                                   const char* sessionKey,
                                   String& body)
{
    WiFiClientSecure client;
    client.setInsecure();  // Cloudflare edge cert; we don't pin it.

    HTTPClient http;
    Serial.printf("[PLAN] GET %s\n", url);
    if (!http.begin(client, url)) {
        Serial.println("[PLAN] http.begin failed");
        return -1;
    }

    // Cookie auth. Nothing else carries auth — no bearer, no CSRF.
    char cookie[320];
    snprintf(cookie, sizeof(cookie), "sessionKey=%s", sessionKey);
    http.addHeader("Cookie", cookie);

    // Chrome-131-like set. Order matters less than presence here.
    http.addHeader("Accept", "*/*");
    http.addHeader("Accept-Language", "en-US,en;q=0.9");
    http.addHeader("Accept-Encoding", "identity");  // skip gzip — ESP32 HTTPClient can't decode
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", UA);
    http.addHeader("Origin", "https://claude.ai");
    http.addHeader("Referer", "https://claude.ai/settings/usage");
    http.addHeader("anthropic-client-platform", "web_claude_ai");
    http.addHeader("anthropic-client-version", "1.0.0");
    http.addHeader("sec-fetch-dest", "empty");
    http.addHeader("sec-fetch-mode", "cors");
    http.addHeader("sec-fetch-site", "same-origin");

    http.setTimeout(15000);

    // Collect Set-Cookie so we can auto-renew the session cookie.
    const char* hdrKeys[] = {"Set-Cookie"};
    http.collectHeaders(hdrKeys, 1);

    int code = http.GET();
    body = http.getString();  // Always try to read — body is useful even on errors
    Serial.printf("[PLAN] HTTP %d, %d bytes\n", code, body.length());
    if (code >= 400) {
        Serial.printf("[PLAN] Error body: %.200s\n", body.c_str());
    }

    // Check for renewed session cookie in response headers.
    if (http.hasHeader("Set-Cookie")) {
        String sc = http.header("Set-Cookie");
        int idx = sc.indexOf("sessionKey=");
        if (idx >= 0) {
            int start = idx + 11;   // strlen("sessionKey=")
            int end   = sc.indexOf(';', start);
            if (end < 0) end = sc.length();
            String key = sc.substring(start, end);
            if (key.length() > 20) {   // sanity — real keys are long
                renewedSession_ = key;
                Serial.println("[PLAN] Session cookie renewed via Set-Cookie");
            }
        }
    }

    http.end();
    return code;
}

// ── Step 1: fetch org UUID (only on first run or after session change) ──
bool ClaudeWebClient::fetchOrgId(const char* sessionKey,
                                 String& outOrgId,
                                 uint8_t& errOut)
{
    String body;
    int code = getWithCookie("https://claude.ai/api/organizations", sessionKey, body);

    if (code < 0) { errOut = CLAUDE_PLAN_NETWORK_ERROR; return false; }

    if (code == 401 || code == 403) {
        // HTML body = CF; JSON body = real auth failure.
        if (body.startsWith("<")) errOut = CLAUDE_PLAN_CF_BLOCKED;
        else                      errOut = CLAUDE_PLAN_AUTH_FAILED;
        return false;
    }
    if (code == 429) { errOut = CLAUDE_PLAN_RATE_LIMITED; return false; }
    if (code != 200) { errOut = CLAUDE_PLAN_NETWORK_ERROR; return false; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err != DeserializationError::Ok) {
        Serial.printf("[PLAN] /organizations JSON parse: %s\n", err.c_str());
        errOut = CLAUDE_PLAN_PARSE_ERROR;
        return false;
    }

    // Response is a top-level array: [{ "uuid": "...", ... }, ...]
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.size() == 0) {
        Serial.println("[PLAN] /organizations: empty array");
        errOut = CLAUDE_PLAN_PARSE_ERROR;
        return false;
    }
    const char* uuid = arr[0]["uuid"] | (const char*)nullptr;
    if (!uuid) {
        Serial.println("[PLAN] /organizations: no uuid field");
        errOut = CLAUDE_PLAN_PARSE_ERROR;
        return false;
    }
    outOrgId = String(uuid);
    Serial.printf("[PLAN] Org UUID: %s\n", outOrgId.c_str());
    errOut = CLAUDE_PLAN_OK;
    return true;
}

// ── Step 2: fetch /usage (the real payload) ──────────────────────
bool ClaudeWebClient::fetchUsage(const char* sessionKey,
                                 const char* orgId,
                                 ClaudePlanSnapshot& out)
{
    char url[192];
    snprintf(url, sizeof(url),
             "https://claude.ai/api/organizations/%s/usage", orgId);

    String body;
    int code = getWithCookie(url, sessionKey, body);

    if (code < 0) { out.error = CLAUDE_PLAN_NETWORK_ERROR; return false; }

    if (code == 401 || code == 403) {
        if (body.startsWith("<")) out.error = CLAUDE_PLAN_CF_BLOCKED;
        else                      out.error = CLAUDE_PLAN_AUTH_FAILED;
        return false;
    }
    if (code == 429) { out.error = CLAUDE_PLAN_RATE_LIMITED; return false; }
    if (code != 200) { out.error = CLAUDE_PLAN_NETWORK_ERROR; return false; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err != DeserializationError::Ok) {
        Serial.printf("[PLAN] /usage JSON parse: %s\n", err.c_str());
        out.error = CLAUDE_PLAN_PARSE_ERROR;
        return false;
    }

    // Required fields. If any is missing, it's a shape drift (parse error).
    JsonVariantConst five = doc["five_hour"];
    JsonVariantConst seven = doc["seven_day"];
    if (five.isNull() || seven.isNull()) {
        Serial.println("[PLAN] /usage: missing five_hour or seven_day");
        out.error = CLAUDE_PLAN_PARSE_ERROR;
        return false;
    }

    out.session_pct       = numOrStr(five["utilization"]);
    out.weekly_pct        = numOrStr(seven["utilization"]);
    out.weekly_opus_pct   = numOrStr(doc["seven_day_opus"]["utilization"]);
    out.weekly_sonnet_pct = numOrStr(doc["seven_day_sonnet"]["utilization"]);

    const char* fr = five["resets_at"]   | (const char*)nullptr;
    const char* sr = seven["resets_at"]  | (const char*)nullptr;
    out.session_resets_at = parseIso8601Utc(fr);
    out.weekly_resets_at  = parseIso8601Utc(sr);

    out.error = CLAUDE_PLAN_OK;
    Serial.printf("[PLAN] session=%.1f%% weekly=%.1f%% opus=%.1f%% sonnet=%.1f%%\n",
        out.session_pct, out.weekly_pct, out.weekly_opus_pct, out.weekly_sonnet_pct);
    return true;
}

// ── Step 3: optional — fetch overage billing ─────────────────────
// 403 / 404 from this endpoint means "user hasn't enabled extra usage";
// treat that as a soft success with extra_enabled = false.
bool ClaudeWebClient::fetchOverage(const char* sessionKey,
                                   const char* orgId,
                                   ClaudePlanSnapshot& out)
{
    char url[192];
    snprintf(url, sizeof(url),
             "https://claude.ai/api/organizations/%s/overage_spend_limit", orgId);

    String body;
    int code = getWithCookie(url, sessionKey, body);

    if (code == 403 || code == 404) {
        // Expected path for users who haven't opted into overage.
        out.extra_enabled       = false;
        out.extra_used_dollars  = 0;
        out.extra_limit_dollars = 0;
        return true;
    }
    if (code != 200) {
        // Don't set out.error here — overage is opportunistic.
        // Just flag extra as unavailable.
        out.extra_enabled       = false;
        out.extra_used_dollars  = 0;
        out.extra_limit_dollars = 0;
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        out.extra_enabled = false;
        return false;
    }

    bool enabled = doc["is_enabled"] | false;
    if (!enabled) {
        out.extra_enabled = false;
        return true;
    }

    // Amounts are in cents; convert to dollars.
    // `monthly_limit` is the new field; `monthly_credit_limit` is a
    // legacy alias we fall back to if the first is missing.
    float limitCents = numOrStr(doc["monthly_limit"]);
    if (limitCents == 0) limitCents = numOrStr(doc["monthly_credit_limit"]);
    float usedCents  = numOrStr(doc["used_credits"]);

    out.extra_enabled       = true;
    out.extra_used_dollars  = usedCents  / 100.0f;
    out.extra_limit_dollars = limitCents / 100.0f;
    Serial.printf("[PLAN] overage: $%.2f / $%.2f\n",
        out.extra_used_dollars, out.extra_limit_dollars);
    return true;
}

// ── Top-level orchestration ──────────────────────────────────────
bool ClaudeWebClient::fetch(const char* sessionKey,
                            const char* cachedOrgId,
                            ClaudePlanSnapshot& out,
                            String& outOrgId,
                            String& outNewSessionKey)
{
    // We DON'T memset out at the start — on failure we want to preserve
    // the caller's cached values (so the screen keeps showing stale data
    // instead of going blank). We only touch fields we successfully read.
    out.error = CLAUDE_PLAN_OK;
    renewedSession_ = "";   // clear before this fetch cycle

    // Step 1 — get (or reuse) the org UUID.
    String orgId = cachedOrgId ? String(cachedOrgId) : String();
    if (orgId.length() == 0) {
        uint8_t err = CLAUDE_PLAN_OK;
        if (!fetchOrgId(sessionKey, orgId, err)) {
            out.error        = err;
            out.last_updated = time(nullptr);
            return false;
        }
        outOrgId = orgId;   // propagate upstream for NVS caching
    } else {
        outOrgId = orgId;   // unchanged, but caller always reads this
    }

    // Step 2 — the core /usage call. If this fails we bail.
    if (!fetchUsage(sessionKey, orgId.c_str(), out)) {
        // On 401/403 the cached org id may also have gone stale — force
        // a re-discovery next call by clearing outOrgId.
        if (out.error == CLAUDE_PLAN_AUTH_FAILED) {
            outOrgId = "";
        }
        out.last_updated = time(nullptr);
        return false;
    }

    // Step 3 — opportunistic overage probe. Never blocks success.
    fetchOverage(sessionKey, orgId.c_str(), out);

    out.valid        = true;
    out.last_updated = time(nullptr);

    // Propagate any renewed session cookie from Set-Cookie headers.
    if (renewedSession_.length() > 0) {
        outNewSessionKey = renewedSession_;
        renewedSession_  = "";
    }
    return true;
}
