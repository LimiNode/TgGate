#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tggate::domain::policy {

enum class PolicyEffect { deny, allow, require_approval };
enum class WritePolicy { deny, require_approval, auto_approve };

struct ToolInvocation {
    std::string tool_name;
    std::string account_id;
    std::optional<std::int64_t> chat_id;
    bool is_write = false;
};

struct McpClientProfile {
    std::string id;
    std::string display_name;
    bool enabled = true;
    std::vector<std::string> allowed_accounts;
    std::vector<std::string> allowed_tools;
    std::vector<std::int64_t> allowed_chats;
    WritePolicy write_policy = WritePolicy::deny;
};

struct PolicyDecision {
    PolicyEffect effect = PolicyEffect::deny;
    std::string reason = "No matching policy rule";

    [[nodiscard]] bool is_allowed() const noexcept { return effect == PolicyEffect::allow; }
};

class IPolicyEngine {
public:
    virtual ~IPolicyEngine() = default;
    [[nodiscard]] virtual PolicyDecision evaluate(
        const McpClientProfile& profile,
        const ToolInvocation& invocation) const = 0;
};

[[nodiscard]] bool contains(const std::vector<std::string>& values, std::string_view value);
[[nodiscard]] bool contains(const std::vector<std::int64_t>& values, std::int64_t value);

} // namespace tggate::domain::policy
