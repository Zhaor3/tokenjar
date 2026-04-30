#include "api/openai_session_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

// Codex CLI OAuth client id, observed from Codex's auth refresh flow.
static const char* CODEX_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann";
static const char* CODEX_USAGE_URL = "https://chatgpt.com/backend-api/codex/usage";
static const char* OAUTH_TOKEN_URL = "https://auth.openai.com/oauth/token";

static const char* UA =
    "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0";

static float numOrStr(JsonVariantConst v) {
    if (v.isNull()) return 0.0f;
    if (v.is<float>() || v.is<double>()) return v.as<float>();
    if (v.is<int>()) return (float)v.as<int>();
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        return s ? (float)atof(s) : 0.0f;
    }
    return 0.0f;
}

static time_t epochOrZero(JsonVariantConst v) {
    if (v.isNull()) return 0;
    if (v.is<long>()) return (time_t)v.as<long>();
    if (v.is<int>()) return (time_t)v.as<int>();
    if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        return s ? (time_t)atol(s) : 0;
    }
    return 0;
}

static void copyStr(char* dst, size_t len, const char* src) {
    if (!dst || len == 0) return;
    if (!src) src = "";
    strncpy(dst, src, len - 1);
    dst[len - 1] = '\0';
}

static void parseWindow(JsonVariantConst w, float& pct, time_t& resetsAt) {
    if (w.isNull()) {
        pct = 0;
        resetsAt = 0;
        return;
    }
    pct = numOrStr(w["used_percent"]);
    resetsAt = epochOrZero(w["reset_at"]);
}

static bool isUrlSafe(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

static String urlEncode(const String& in) {
    static const char* HEX_DIGITS = "0123456789ABCDEF";
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); ++i) {
        char c = in[i];
        if (isUrlSafe(c)) {
            out += c;
        } else {
            out += '%';
            out += HEX_DIGITS[(uint8_t)c >> 4];
            out += HEX_DIGITS[(uint8_t)c & 0x0F];
        }
    }
    return out;
}

bool OpenAISessionClient::looksLikeAPIKey(const char* token) {
    if (!token) return false;
    while (*token == ' ' || *token == '\t' || *token == '\r' || *token == '\n') token++;
    return strncmp(token, "sk-", 3) == 0;
}

int OpenAISessionClient::getUsage(const char* accessToken,
                                  const char* accountId,
                                  String& body)
{
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    Serial.printf("[CODEX] GET %s\n", CODEX_USAGE_URL);
    if (!http.begin(client, CODEX_USAGE_URL)) {
        Serial.println("[CODEX] http.begin failed");
        return -1;
    }

    http.addHeader("Authorization", String("Bearer ") + accessToken);
    if (accountId && *accountId) {
        http.addHeader("chatgpt-account-id", accountId);
    }
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", UA);
    http.setTimeout(15000);

    int code = http.GET();
    body = http.getString();
    Serial.printf("[CODEX] HTTP %d, %d bytes\n", code, body.length());
    if (code >= 400) {
        Serial.printf("[CODEX] Error body: %.200s\n", body.c_str());
    }
    http.end();
    return code;
}

bool OpenAISessionClient::refreshAccessToken(const char* refreshToken,
                                             String& outAccessToken,
                                             String& outRefreshToken)
{
    if (!refreshToken || !*refreshToken) return false;
    if (looksLikeAPIKey(refreshToken)) {
        Serial.println("[CODEX] refresh token looks like an OpenAI API key; need Codex auth.json token");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    Serial.printf("[CODEX] POST %s\n", OAUTH_TOKEN_URL);
    if (!http.begin(client, OAUTH_TOKEN_URL)) {
        Serial.println("[CODEX] token http.begin failed");
        return false;
    }

    String body = "grant_type=refresh_token&refresh_token=";
    body += urlEncode(String(refreshToken));
    body += "&client_id=";
    body += urlEncode(String(CODEX_CLIENT_ID));

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Accept", "application/json");
    http.setTimeout(15000);

    int code = http.POST(body);
    String resp = http.getString();
    Serial.printf("[CODEX] token HTTP %d, %d bytes\n", code, resp.length());
    if (code != HTTP_CODE_OK) {
        Serial.printf("[CODEX] token error: %.200s\n", resp.c_str());
        http.end();
        return false;
    }
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);
    if (err != DeserializationError::Ok) {
        Serial.printf("[CODEX] token JSON parse: %s\n", err.c_str());
        return false;
    }

    const char* access = doc["access_token"] | (const char*)nullptr;
    if (!access || !*access) {
        Serial.println("[CODEX] token response missing access_token");
        return false;
    }

    outAccessToken = String(access);
    const char* refresh = doc["refresh_token"] | (const char*)nullptr;
    if (refresh && *refresh) outRefreshToken = String(refresh);
    return true;
}

bool OpenAISessionClient::parseUsage(const String& body,
                                     CodexPlanSnapshot& out)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err != DeserializationError::Ok) {
        Serial.printf("[CODEX] usage JSON parse: %s\n", err.c_str());
        return false;
    }

    JsonVariantConst rl = doc["rate_limit"];
    JsonVariantConst primary = rl["primary_window"];
    JsonVariantConst secondary = rl["secondary_window"];
    if (primary.isNull() || secondary.isNull()) {
        Serial.println("[CODEX] usage missing primary/secondary windows");
        return false;
    }

    memset(&out, 0, sizeof(out));
    copyStr(out.plan_type, sizeof(out.plan_type), doc["plan_type"] | "");

    parseWindow(primary, out.session_pct, out.session_resets_at);
    parseWindow(secondary, out.weekly_pct, out.weekly_resets_at);

    // Pick the additional limit with the highest utilization so the device
    // shows the most relevant model-specific cap without needing a list UI.
    float bestModelPct = 0.0f;
    time_t bestModelReset = 0;
    const char* bestModelName = "";
    for (JsonObjectConst item : doc["additional_rate_limits"].as<JsonArrayConst>()) {
        const char* name = item["limit_name"] | "";
        JsonVariantConst sub = item["rate_limit"];

        float pPct = 0, sPct = 0;
        time_t pReset = 0, sReset = 0;
        parseWindow(sub["primary_window"], pPct, pReset);
        parseWindow(sub["secondary_window"], sPct, sReset);

        float itemPct = (pPct >= sPct) ? pPct : sPct;
        time_t itemReset = (pPct >= sPct) ? pReset : sReset;
        if (itemPct > bestModelPct) {
            bestModelPct = itemPct;
            bestModelReset = itemReset;
            bestModelName = name;
        }
    }

    if (bestModelPct > 0.01f) {
        out.has_model_limit = true;
        out.model_pct = bestModelPct;
        out.model_resets_at = bestModelReset;
        copyStr(out.model_name, sizeof(out.model_name), bestModelName);
    }

    JsonVariantConst cr = doc["code_review_rate_limit"]["primary_window"];
    if (!cr.isNull()) {
        parseWindow(cr, out.code_review_pct, out.code_review_resets_at);
        out.has_code_review = out.code_review_pct > 0.01f;
    }

    out.error = CODEX_PLAN_OK;
    out.valid = true;
    out.last_updated = time(nullptr);

    Serial.printf("[CODEX] session=%.1f%% weekly=%.1f%% model=%.1f%% review=%.1f%% plan=%s\n",
        out.session_pct, out.weekly_pct, out.model_pct, out.code_review_pct, out.plan_type);
    return true;
}

bool OpenAISessionClient::fetch(const char* accessToken,
                                const char* accountId,
                                CodexPlanSnapshot& out)
{
    if (!accessToken || !*accessToken) {
        out.error = CODEX_PLAN_AUTH_FAILED;
        out.last_updated = time(nullptr);
        return false;
    }
    if (looksLikeAPIKey(accessToken)) {
        out.error = CODEX_PLAN_WRONG_TOKEN;
        out.last_updated = time(nullptr);
        return false;
    }

    String body;
    int code = getUsage(accessToken, accountId, body);
    if (code < 0) {
        out.error = CODEX_PLAN_NETWORK_ERROR;
        out.last_updated = time(nullptr);
        return false;
    }
    if (code == 401 || code == 403) {
        out.error = CODEX_PLAN_AUTH_FAILED;
        out.last_updated = time(nullptr);
        return false;
    }
    if (code == 429) {
        out.error = CODEX_PLAN_RATE_LIMITED;
        out.last_updated = time(nullptr);
        return false;
    }
    if (code != HTTP_CODE_OK) {
        out.error = CODEX_PLAN_NETWORK_ERROR;
        out.last_updated = time(nullptr);
        return false;
    }

    CodexPlanSnapshot parsed;
    if (!parseUsage(body, parsed)) {
        out.error = CODEX_PLAN_PARSE_ERROR;
        out.last_updated = time(nullptr);
        return false;
    }

    out = parsed;
    return true;
}
