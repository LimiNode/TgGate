#pragma once

#include "application/AuditService.hpp"
#include "application/TelegramPort.hpp"
#include "domain/approval/ApprovalService.hpp"
#include "domain/policy/Policy.hpp"

#include <nlohmann/json.hpp>

namespace tggate::application {

class TelegramApplicationService final {
public:
    TelegramApplicationService(
        ITelegramService& telegram,
        const domain::policy::IPolicyEngine& policy_engine,
        domain::approval::ApprovalService& approvals,
        AuditService& audit);

    [[nodiscard]] nlohmann::json list_chats(
        const domain::policy::McpClientProfile& profile,
        std::string_view account_id);
    [[nodiscard]] nlohmann::json get_messages(
        const domain::policy::McpClientProfile& profile,
        std::string_view account_id,
        std::int64_t chat_id,
        std::size_t limit);
    [[nodiscard]] nlohmann::json prepare_send_message(
        const domain::policy::McpClientProfile& profile,
        std::string_view account_id,
        std::int64_t chat_id,
        std::string_view text);
    [[nodiscard]] nlohmann::json execute_approved_action(
        const domain::policy::McpClientProfile& profile,
        std::string_view action_id);

private:
    [[nodiscard]] nlohmann::json deny_and_audit(
        const domain::policy::McpClientProfile& profile,
        const domain::policy::ToolInvocation& invocation,
        const domain::policy::PolicyDecision& decision);
    void audit(
        const domain::policy::McpClientProfile& profile,
        const domain::policy::ToolInvocation& invocation,
        domain::policy::PolicyEffect decision,
        std::string result_status,
        std::optional<std::string> action_id = std::nullopt);

    ITelegramService& telegram_;
    const domain::policy::IPolicyEngine& policy_engine_;
    domain::approval::ApprovalService& approvals_;
    AuditService& audit_service_;
};

} // namespace tggate::application
