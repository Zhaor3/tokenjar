#pragma once
#include "api/usage_provider.h"

class ClaudeUsageClient : public IUsageProvider {
public:
    bool fetch(const char* apiKey, UsageSnapshot& out) override;
    const char* name() const override { return "claude"; }
};
