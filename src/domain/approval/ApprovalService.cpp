#include "domain/approval/ApprovalService.hpp"

#include <array>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace tggate::domain::approval {
namespace {

constexpr auto kHex = "0123456789abcdef";

} // namespace

std::string ApprovalService::create_secure_id() {
    std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        throw std::runtime_error("BCryptGenRandom failed");
    }
#else
    // Windows is the supported production target. Keep a portable fallback for core tests.
    std::random_device random_device;
    for (auto& byte : bytes) {
        byte = static_cast<unsigned char>(random_device());
    }
#endif

    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(kHex[byte >> 4]);
        result.push_back(kHex[byte & 0x0f]);
    }
    return result;
}

bool ApprovalService::is_expired(const PendingAction& action) {
    return std::chrono::system_clock::now() >= action.expires_at;
}

PendingAction ApprovalService::prepare(
    std::string client_id,
    policy::ToolInvocation invocation,
    nlohmann::json arguments,
    const std::chrono::seconds lifetime) {
    const auto now = std::chrono::system_clock::now();
    PendingAction action{
        .id = create_secure_id(),
        .client_id = std::move(client_id),
        .invocation = std::move(invocation),
        .arguments = std::move(arguments),
        .created_at = now,
        .expires_at = now + lifetime,
    };

    std::scoped_lock lock(mutex_);
    actions_.emplace(action.id, action);
    return action;
}

std::optional<PendingAction> ApprovalService::find_and_expire_locked(const std::string_view action_id) {
    const auto iterator = actions_.find(std::string(action_id));
    if (iterator == actions_.end()) {
        return std::nullopt;
    }
    if (iterator->second.status == ActionStatus::pending && is_expired(iterator->second)) {
        iterator->second.status = ActionStatus::expired;
    }
    return iterator->second;
}

std::optional<PendingAction> ApprovalService::approve(const std::string_view action_id) {
    std::scoped_lock lock(mutex_);
    const auto action = find_and_expire_locked(action_id);
    if (!action || action->status != ActionStatus::pending) {
        return std::nullopt;
    }
    actions_.at(action->id).status = ActionStatus::approved;
    return actions_.at(action->id);
}

std::optional<PendingAction> ApprovalService::deny(const std::string_view action_id) {
    std::scoped_lock lock(mutex_);
    const auto action = find_and_expire_locked(action_id);
    if (!action || action->status != ActionStatus::pending) {
        return std::nullopt;
    }
    actions_.at(action->id).status = ActionStatus::denied;
    return actions_.at(action->id);
}

std::optional<PendingAction> ApprovalService::take_approved_for_execution(
    const std::string_view action_id,
    const policy::McpClientProfile& current_profile,
    const policy::IPolicyEngine& policy_engine) {
    std::scoped_lock lock(mutex_);
    const auto action = find_and_expire_locked(action_id);
    if (!action || action->status != ActionStatus::approved || action->client_id != current_profile.id) {
        return std::nullopt;
    }

    const auto decision = policy_engine.evaluate(current_profile, action->invocation);
    if (decision.effect == policy::PolicyEffect::deny) {
        actions_.at(action->id).status = ActionStatus::denied;
        return std::nullopt;
    }

    actions_.at(action->id).status = ActionStatus::executed;
    return actions_.at(action->id);
}

std::vector<PendingAction> ApprovalService::pending_actions() const {
    std::scoped_lock lock(mutex_);
    std::vector<PendingAction> result;
    for (const auto& [id, action] : actions_) {
        if (action.status == ActionStatus::pending || action.status == ActionStatus::approved) {
            result.push_back(action);
        }
    }
    return result;
}

void ApprovalService::lockdown() {
    std::scoped_lock lock(mutex_);
    for (auto& [id, action] : actions_) {
        if (action.status == ActionStatus::pending || action.status == ActionStatus::approved) {
            action.status = ActionStatus::denied;
        }
    }
}

} // namespace tggate::domain::approval
