#pragma once
#include "api/usage_provider.h"

class OpenAIUsageClient : public IUsageProvider {
public:
    bool fetch(const char* apiKey, UsageSnapshot& out) override;
    const char* name() const override { return "openai"; }
};
