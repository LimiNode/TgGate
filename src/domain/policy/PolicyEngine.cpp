#include "domain/policy/PolicyEngine.hpp"

namespace tggate::domain::policy {

bool contains(const std::vector<std::string>& values, const std::string_view value) {
    return std::ranges::find(values, value) != values.end();
}

bool contains(const std::vector<std::int64_t>& values, const std::int64_t value) {
    return std::ranges::find(values, value) != values.end();
}

PolicyDecision PolicyEngine::evaluate(
    const McpClientProfile& profile,
    const ToolInvocation& invocation) const {
    if (!profile.enabled) {
        return {PolicyEffect::deny, "Client profile is disabled"};
    }
    if (!contains(profile.allowed_accounts, invocation.account_id)) {
        return {PolicyEffect::deny, "Account is not allowed for this client"};
    }
    if (!contains(profile.allowed_tools, invocation.tool_name)) {
        return {PolicyEffect::deny, "Tool is not allowed for this client"};
    }
    if (invocation.chat_id && !contains(profile.allowed_chats, *invocation.chat_id)) {
        return {PolicyEffect::deny, "Chat is not allowlisted for this client"};
    }
    if (!invocation.is_write) {
        return {PolicyEffect::allow, "Read operation is allowed"};
    }

    switch (profile.write_policy) {
    case WritePolicy::auto_approve:
        return {PolicyEffect::allow, "Write operation is explicitly auto-approved"};
    case WritePolicy::require_approval:
        return {PolicyEffect::require_approval, "Write operation requires local approval"};
    case WritePolicy::deny:
        return {PolicyEffect::deny, "Write operations are denied"};
    }
    return {PolicyEffect::deny, "Unknown write policy"};
}

} // namespace tggate::domain::policy
