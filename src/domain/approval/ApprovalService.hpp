#pragma once

#include "domain/policy/Policy.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace tggate::domain::approval {

enum class ActionStatus { pending, approved, denied, executed, expired };

struct PendingAction {
    std::string id;
    std::string client_id;
    policy::ToolInvocation invocation;
    nlohmann::json arguments;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    ActionStatus status = ActionStatus::pending;
};

class ApprovalService final {
public:
    [[nodiscard]] PendingAction prepare(
        std::string client_id,
        policy::ToolInvocation invocation,
        nlohmann::json arguments,
        std::chrono::seconds lifetime = std::chrono::minutes(2));

    [[nodiscard]] std::optional<PendingAction> approve(std::string_view action_id);
    [[nodiscard]] std::optional<PendingAction> deny(std::string_view action_id);
    [[nodiscard]] std::optional<PendingAction> take_approved_for_execution(
        std::string_view action_id,
        const policy::McpClientProfile& current_profile,
        const policy::IPolicyEngine& policy_engine);
    [[nodiscard]] std::vector<PendingAction> pending_actions() const;
    void lockdown();

private:
    [[nodiscard]] static std::string create_secure_id();
    [[nodiscard]] static bool is_expired(const PendingAction& action);
    [[nodiscard]] std::optional<PendingAction> find_and_expire_locked(std::string_view action_id);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, PendingAction> actions_;
};

} // namespace tggate::domain::approval
