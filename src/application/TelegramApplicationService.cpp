#include "application/TelegramApplicationService.hpp"

#include <algorithm>

namespace tggate::application {
namespace {

constexpr auto kExternalTrust = "untrusted_external_content";

nlohmann::json error(const std::string_view message) {
    return {{"ok", false}, {"error", message}};
}

std::string sanitize_external_text(std::string text) {
    text.erase(std::remove_if(text.begin(), text.end(), [](const unsigned char character) {
        return character < 0x20 && character != '\n' && character != '\t';
    }), text.end());
    constexpr std::size_t kMaximumExternalTextBytes = 8 * 1024;
    if (text.size() > kMaximumExternalTextBytes) {
        text.resize(kMaximumExternalTextBytes);
        text.append("… [truncated]");
    }
    return text;
}

} // namespace

Result<std::vector<Chat>> UnavailableTelegramService::list_chats(std::string_view) {
    return std::string("TDLib account is not configured");
}

Result<std::vector<Message>> UnavailableTelegramService::get_messages(std::string_view, std::int64_t, std::size_t) {
    return std::string("TDLib account is not configured");
}

Result<Message> UnavailableTelegramService::send_message(std::string_view, std::int64_t, std::string_view) {
    return std::string("TDLib account is not configured");
}

TelegramApplicationService::TelegramApplicationService(
    ITelegramService& telegram,
    const domain::policy::IPolicyEngine& policy_engine,
    domain::approval::ApprovalService& approvals,
    AuditService& audit)
    : telegram_(telegram), policy_engine_(policy_engine), approvals_(approvals), audit_service_(audit) {}

void TelegramApplicationService::audit(
    const domain::policy::McpClientProfile& profile,
    const domain::policy::ToolInvocation& invocation,
    const domain::policy::PolicyEffect decision,
    std::string result_status,
    std::optional<std::string> action_id) {
    audit_service_.record({
        .timestamp = std::chrono::system_clock::now(),
        .client_id = profile.id,
        .account_id = invocation.account_id,
        .tool_name = invocation.tool_name,
        .chat_id = invocation.chat_id,
        .decision = decision,
        .result_status = std::move(result_status),
        .action_id = std::move(action_id),
    });
}

nlohmann::json TelegramApplicationService::deny_and_audit(
    const domain::policy::McpClientProfile& profile,
    const domain::policy::ToolInvocation& invocation,
    const domain::policy::PolicyDecision& decision) {
    audit(profile, invocation, decision.effect, "denied");
    return error(decision.reason);
}

nlohmann::json TelegramApplicationService::list_chats(
    const domain::policy::McpClientProfile& profile,
    const std::string_view account_id) {
    const domain::policy::ToolInvocation invocation{
        .tool_name = "telegram_list_chats", .account_id = std::string(account_id)};
    const auto decision = policy_engine_.evaluate(profile, invocation);
    if (!decision.is_allowed()) {
        return deny_and_audit(profile, invocation, decision);
    }
    const auto chats = telegram_.list_chats(account_id);
    if (!chats) {
        audit(profile, invocation, decision.effect, "error");
        return error(chats.error());
    }
    nlohmann::json result = nlohmann::json::array();
    for (const auto& chat : *chats) {
        // A backend must still omit chats that are not in the profile allowlist.
        if (domain::policy::contains(profile.allowed_chats, chat.id)) {
            result.push_back(nlohmann::json{{"id", chat.id}, {"title", chat.title}, {"source", "telegram"}, {"trust", kExternalTrust}});
        }
    }
    audit(profile, invocation, decision.effect, "ok");
    return {{"ok", true}, {"chats", std::move(result)}};
}

nlohmann::json TelegramApplicationService::get_messages(
    const domain::policy::McpClientProfile& profile,
    const std::string_view account_id,
    const std::int64_t chat_id,
    const std::size_t limit) {
    const domain::policy::ToolInvocation invocation{
        .tool_name = "telegram_get_messages", .account_id = std::string(account_id), .chat_id = chat_id};
    const auto decision = policy_engine_.evaluate(profile, invocation);
    if (!decision.is_allowed()) {
        return deny_and_audit(profile, invocation, decision);
    }
    const auto messages = telegram_.get_messages(account_id, chat_id, std::min(limit, std::size_t{100}));
    if (!messages) {
        audit(profile, invocation, decision.effect, "error");
        return error(messages.error());
    }
    nlohmann::json result = nlohmann::json::array();
    for (const auto& message : *messages) {
        result.push_back(nlohmann::json{
            {"id", message.id}, {"chat_id", message.chat_id}, {"text", sanitize_external_text(message.text)},
            {"source", "telegram"}, {"trust", kExternalTrust},
        });
    }
    audit(profile, invocation, decision.effect, "ok");
    return {{"ok", true}, {"messages", std::move(result)}};
}

nlohmann::json TelegramApplicationService::prepare_send_message(
    const domain::policy::McpClientProfile& profile,
    const std::string_view account_id,
    const std::int64_t chat_id,
    const std::string_view text) {
    if (text.empty() || text.size() > 4096) {
        return error("Message must contain 1..4096 bytes");
    }
    const domain::policy::ToolInvocation invocation{
        .tool_name = "telegram_prepare_send_message", .account_id = std::string(account_id),
        .chat_id = chat_id, .is_write = true};
    const auto decision = policy_engine_.evaluate(profile, invocation);
    if (decision.effect == domain::policy::PolicyEffect::deny) {
        return deny_and_audit(profile, invocation, decision);
    }
    const auto action = approvals_.prepare(profile.id, invocation, {{"text", text}});
    audit(profile, invocation, decision.effect, "prepared", action.id);
    return {{"ok", true}, {"action_id", action.id}, {"expires_at", action.expires_at.time_since_epoch().count()},
        {"status", "pending_local_approval"}};
}

nlohmann::json TelegramApplicationService::execute_approved_action(
    const domain::policy::McpClientProfile& profile,
    const std::string_view action_id) {
    const auto action = approvals_.take_approved_for_execution(action_id, profile, policy_engine_);
    if (!action) {
        return error("Action is absent, not approved, expired, already executed, or no longer allowed");
    }
    const auto response = telegram_.send_message(
        action->invocation.account_id,
        *action->invocation.chat_id,
        action->arguments.at("text").get<std::string>());
    if (!response) {
        audit(profile, action->invocation, domain::policy::PolicyEffect::allow, "error", action->id);
        return error(response.error());
    }
    audit(profile, action->invocation, domain::policy::PolicyEffect::allow, "ok", action->id);
    return {{"ok", true}, {"message_id", response->id}, {"chat_id", response->chat_id}};
}

} // namespace tggate::application
