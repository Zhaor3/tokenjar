#include "api/openai_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// OpenAI Admin Usage API.
// Docs: https://platform.openai.com/docs/api-reference/usage
// Costs endpoint expected shape:
// {
//   "data": [ { "results": [ { "amount": { "value": 1247 },
//               "line_item": "...", ... } ] } ],
//   "has_more": false
// }
// Completions usage expected shape:
// {
//   "data": [ { "results": [ { "input_tokens": 500000,
//               "output_tokens": 200000, ... } ] } ]
// }

static time_t startOfDayUTC() {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
    return mktime(&t);
}

static time_t startOfMonthUTC() {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    t.tm_mday = 1; t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
    return mktime(&t);
}

static bool oaiGet(const char* url, const char* apiKey, String& body) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (!http.begin(client, url)) return false;
    http.addHeader("Authorization", String("Bearer ") + apiKey);
    http.addHeader("Content-Type", "application/json");
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

static float parseCosts(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return 0;

    float total = 0;
    for (JsonObject bucket : doc["data"].as<JsonArray>()) {
        for (JsonObject r : bucket["results"].as<JsonArray>()) {
            total += (r["amount"]["value"] | 0.0f) / 100.0f;   // cents → dollars
        }
    }
    return total;
}

static void parseTokens(const String& body, uint64_t& out) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return;

    for (JsonObject bucket : doc["data"].as<JsonArray>()) {
        for (JsonObject r : bucket["results"].as<JsonArray>()) {
            out += (uint64_t)(r["input_tokens"] | 0) +
                   (uint64_t)(r["output_tokens"] | 0);
        }
    }
}

bool OpenAIUsageClient::fetch(const char* apiKey, UsageSnapshot& out) {
    memset(&out, 0, sizeof(out));

    time_t dayStart   = startOfDayUTC();
    time_t monthStart = startOfMonthUTC();

    // ── Today's costs ────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.openai.com/v1/organization/costs"
            "?start_time=%ld&limit=30",
            (long)dayStart);

        String body;
        if (oaiGet(url, apiKey, body))
            out.spend_today = parseCosts(body);
    }

    // ── Month costs ──────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.openai.com/v1/organization/costs"
            "?start_time=%ld&limit=100",
            (long)monthStart);

        String body;
        if (oaiGet(url, apiKey, body))
            out.spend_month = parseCosts(body);
    }

    // ── Today's tokens ───────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.openai.com/v1/organization/usage/completions"
            "?start_time=%ld&limit=30",
            (long)dayStart);

        String body;
        if (oaiGet(url, apiKey, body))
            parseTokens(body, out.tokens_today);
    }

    // ── Month tokens ─────────────────────────────────────────────
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.openai.com/v1/organization/usage/completions"
            "?start_time=%ld&limit=100",
            (long)monthStart);

        String body;
        if (oaiGet(url, apiKey, body))
            parseTokens(body, out.tokens_month);
    }

    // Build sparkline from daily spend distributed across elapsed hours
    time_t now = time(nullptr);
    struct tm tn;
    localtime_r(&now, &tn);
    int hours_today = tn.tm_hour + 1;
    float per_hour = (hours_today > 0) ? out.spend_today / hours_today : 0;
    for (int i = 0; i < SPARK_POINTS; i++)
        out.hourly_spend[i] = (i >= SPARK_POINTS - hours_today) ? per_hour : 0;

    out.last_updated = now;
    out.valid = (out.last_updated > 1000000);
    return out.valid;
}
