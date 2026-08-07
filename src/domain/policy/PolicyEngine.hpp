#pragma once

#include "domain/policy/Policy.hpp"

namespace tggate::domain::policy {

class PolicyEngine final : public IPolicyEngine {
public:
    [[nodiscard]] PolicyDecision evaluate(
        const McpClientProfile& profile,
        const ToolInvocation& invocation) const override;
};

} // namespace tggate::domain::policy
