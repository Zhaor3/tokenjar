#include "api/claude_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Anthropic Admin API
// Docs: https://platform.claude.com/docs/en/api/admin-api/usage-cost
// Cost report: GET /v1/organizations/cost_report?starting_at=...&ending_at=...
//   Response: { "data": [ { "results": [ { "amount": "123.45", "currency": "USD", ... } ] } ] }
//   amount is a STRING in CENTS (lowest currency unit)
// Usage report: GET /v1/organizations/usage_report/messages?starting_at=...&ending_at=...
//   Response: { "data": [ { "results": [ { "uncached_input_tokens": N, "output_tokens": N, ... } ] } ] }

// RFC 3339 date formatters (UTC)
static void todayStartUTC(char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    strftime(buf, len, "%Y-%m-%dT00:00:00Z", &t);
}

static void tomorrowStartUTC(char* buf, size_t len) {
    time_t now = time(nullptr) + 86400;
    struct tm t;
    gmtime_r(&now, &t);
    snprintf(buf, len, "%04d-%02d-%02dT00:00:00Z", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

static void monthStartUTC(char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    snprintf(buf, len, "%04d-%02d-01T00:00:00Z", t.tm_year + 1900, t.tm_mon + 1);
}

static bool apiGet(const char* url, const char* apiKey, String& body) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    Serial.printf("[CLAUDE] GET %s\n", url);
    if (!http.begin(client, url)) {
        Serial.println("[CLAUDE] http.begin failed");
        return false;
    }
    http.addHeader("x-api-key", apiKey);
    http.addHeader("anthropic-version", "2023-06-01");
    http.setTimeout(15000);

    int code = http.GET();
    if (code == HTTP_CODE_OK) {
        body = http.getString();
        Serial.printf("[CLAUDE] HTTP 200 — %d bytes\n", body.length());
        http.end();
        return true;
    }
    // Print error body for debugging
    String errBody = http.getString();
    Serial.printf("[CLAUDE] HTTP %d for %s\n", code, url);
    Serial.printf("[CLAUDE] Error: %.200s\n", errBody.c_str());
    http.end();
    return false;
}

// Parse cost from data[].results[].amount (string in cents)
static float parseCostResponse(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[CLAUDE] JSON parse failed");
        return 0;
    }
    float total = 0;
    int count = 0;
    for (JsonObject bucket : doc["data"].as<JsonArray>()) {
        for (JsonObject r : bucket["results"].as<JsonArray>()) {
            // amount is a decimal string in cents, e.g. "123.45" = $1.2345
            float cents = 0;
            if (r["amount"].is<const char*>()) {
                cents = String(r["amount"].as<const char*>()).toFloat();
            } else {
                cents = r["amount"].as<float>();
            }
            total += cents / 100.0f;
            count++;
        }
    }
    Serial.printf("[CLAUDE] Parsed %d cost entries, total=$%.4f\n", count, total);
    return total;
}

// Parse tokens from data[].results[]
static uint64_t parseTokenResponse(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[CLAUDE] JSON parse failed (tokens)");
        return 0;
    }
    uint64_t total = 0;
    for (JsonObject bucket : doc["data"].as<JsonArray>()) {
        for (JsonObject r : bucket["results"].as<JsonArray>()) {
            total += (uint64_t)(r["uncached_input_tokens"] | 0) +
                     (uint64_t)(r["output_tokens"] | 0) +
                     (uint64_t)(r["cache_read_input_tokens"] | 0);
        }
    }
    return total;
}

bool ClaudeUsageClient::fetch(const char* apiKey, UsageSnapshot& out) {
    memset(&out, 0, sizeof(out));
    bool gotData = false;

    char todayStart[32], tomorrowStart[32], monthStart[32];
    todayStartUTC(todayStart, sizeof(todayStart));
    tomorrowStartUTC(tomorrowStart, sizeof(tomorrowStart));
    monthStartUTC(monthStart, sizeof(monthStart));

    Serial.printf("[CLAUDE] Date range: today=%s tomorrow=%s month=%s\n",
        todayStart, tomorrowStart, monthStart);

    // ── Today's cost ─────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/cost_report"
            "?starting_at=%s&ending_at=%s",
            todayStart, tomorrowStart);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            Serial.printf("[CLAUDE] Today cost raw: %.400s\n", body.c_str());
            out.spend_today = parseCostResponse(body);
        } else {
            Serial.println("[CLAUDE] Today cost fetch FAILED");
        }
    }

    // ── Month cost ───────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/cost_report"
            "?starting_at=%s&ending_at=%s",
            monthStart, tomorrowStart);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            Serial.printf("[CLAUDE] Month cost raw: %.200s...\n", body.c_str());
            out.spend_month = parseCostResponse(body);
        }
    }

    // ── Today's tokens ───────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/usage_report/messages"
            "?starting_at=%s&ending_at=%s&bucket_width=1d",
            todayStart, tomorrowStart);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            Serial.printf("[CLAUDE] Today tokens raw: %.400s\n", body.c_str());
            out.tokens_today = parseTokenResponse(body);
            Serial.printf("[CLAUDE] tokens_today = %llu\n", (unsigned long long)out.tokens_today);
        }
    }

    // ── Month tokens ─────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/usage_report/messages"
            "?starting_at=%s&ending_at=%s&bucket_width=1d",
            monthStart, tomorrowStart);

        String body;
        if (apiGet(url, apiKey, body)) {
            gotData = true;
            out.tokens_month = parseTokenResponse(body);
            Serial.printf("[CLAUDE] tokens_month = %llu\n", (unsigned long long)out.tokens_month);
        }
    }

    // Build sparkline (distribute daily cost across elapsed hours)
    time_t now = time(nullptr);
    struct tm tn;
    localtime_r(&now, &tn);
    int hours_today = tn.tm_hour + 1;
    float per_hour = (hours_today > 0) ? out.spend_today / hours_today : 0;
    for (int i = 0; i < SPARK_POINTS; i++)
        out.hourly_spend[i] = (i >= SPARK_POINTS - hours_today) ? per_hour : 0;

    out.last_updated = time(nullptr);
    out.valid = gotData;
    Serial.printf("[CLAUDE] FINAL: today=$%.4f month=$%.4f tok_today=%llu tok_month=%llu valid=%d\n",
        out.spend_today, out.spend_month,
        (unsigned long long)out.tokens_today, (unsigned long long)out.tokens_month,
        out.valid);
    return out.valid;
}
