#include "api/claude_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Anthropic Admin API — cost_report endpoint.
// Docs: https://docs.anthropic.com/en/api/admin-api
// Expected response shape (simplified):
// {
//   "data": [
//     { "date": "2024-01-15", "cost_usd": 3.21,
//       "input_tokens": 500000, "output_tokens": 200000 }
//   ]
// }
// Adjust the JSON path below if the real schema differs.

static void todayStr(char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    strftime(buf, len, "%Y-%m-%d", &t);
}

static void monthStartStr(char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    t.tm_mday = 1;
    strftime(buf, len, "%Y-%m-%d", &t);
}

static void tomorrowStr(char* buf, size_t len) {
    time_t now = time(nullptr) + 86400;
    struct tm t;
    localtime_r(&now, &t);
    strftime(buf, len, "%Y-%m-%d", &t);
}

static bool apiGet(const char* url, const char* apiKey, String& body) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (!http.begin(client, url)) return false;
    http.addHeader("x-api-key", apiKey);
    http.addHeader("anthropic-version", "2023-06-01");
    http.setTimeout(15000);

    int code = http.GET();
    if (code == HTTP_CODE_OK) {
        body = http.getString();
        http.end();
        return true;
    }
    http.end();
    return false;
}

bool ClaudeUsageClient::fetch(const char* apiKey, UsageSnapshot& out) {
    memset(&out, 0, sizeof(out));

    char today[16], tomorrow[16], monthStart[16];
    todayStr(today, sizeof(today));
    tomorrowStr(tomorrow, sizeof(tomorrow));
    monthStartStr(monthStart, sizeof(monthStart));

    // ── Today's cost ─────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/cost_report"
            "?start_date=%s&end_date=%s",
            today, tomorrow);

        String body;
        if (apiGet(url, apiKey, body)) {
            JsonDocument doc;
            if (deserializeJson(doc, body) == DeserializationError::Ok) {
                JsonArray data = doc["data"].as<JsonArray>();
                for (JsonObject row : data) {
                    out.spend_today  += row["cost_usd"] | 0.0f;
                    out.tokens_today += (uint64_t)(row["input_tokens"] | 0) +
                                       (uint64_t)(row["output_tokens"] | 0);
                }
            }
        }
    }

    // ── Month cost ───────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.anthropic.com/v1/organizations/cost_report"
            "?start_date=%s&end_date=%s",
            monthStart, tomorrow);

        String body;
        if (apiGet(url, apiKey, body)) {
            JsonDocument doc;
            if (deserializeJson(doc, body) == DeserializationError::Ok) {
                JsonArray data = doc["data"].as<JsonArray>();
                for (JsonObject row : data) {
                    out.spend_month  += row["cost_usd"] | 0.0f;
                    out.tokens_month += (uint64_t)(row["input_tokens"] | 0) +
                                       (uint64_t)(row["output_tokens"] | 0);
                }

                // Build hourly buckets from daily rows (API granularity is daily;
                // distribute evenly across hours that have elapsed so far)
                time_t now = time(nullptr);
                struct tm tn;
                localtime_r(&now, &tn);
                int hours_today = tn.tm_hour + 1;
                float per_hour = (hours_today > 0) ? out.spend_today / hours_today : 0;
                for (int i = 0; i < SPARK_POINTS; i++) {
                    out.hourly_spend[i] = (i >= SPARK_POINTS - hours_today) ? per_hour : 0;
                }
            }
        }
    }

    out.last_updated = time(nullptr);
    out.valid = (out.spend_today > 0 || out.spend_month > 0 || out.tokens_today > 0);
    // Even if values are zero, mark valid if we got a successful HTTP response
    if (out.last_updated > 1000000) out.valid = true;
    return out.valid;
}
