#pragma once
#include <Arduino.h>
#include "api/codex_plan.h"

// OpenAI Codex / ChatGPT usage client.
//
// Uses the same ChatGPT backend endpoint Codex CLI reads for plan limits:
//   GET https://chatgpt.com/backend-api/codex/usage
//
// Auth is OAuth bearer-token based. Access tokens expire regularly, so callers
// should store a refresh token too and call refreshAccessToken() after auth
// failures or when only a refresh token is available.
class OpenAISessionClient {
public:
    bool fetch(const char* accessToken,
               const char* accountId,
               CodexPlanSnapshot& out);

    bool refreshAccessToken(const char* refreshToken,
                            String& outAccessToken,
                            String& outRefreshToken);

    static bool looksLikeAPIKey(const char* token);

private:
    int  getUsage(const char* accessToken,
                  const char* accountId,
                  String& body);
    bool parseUsage(const String& body, CodexPlanSnapshot& out);
};
