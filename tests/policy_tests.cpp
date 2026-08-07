#include "application/AuditService.hpp"
#include "application/config/ClientProfileRepository.hpp"
#include "application/config/AccountRepository.hpp"
#include "application/config/McpHostConfigurationRepository.hpp"
#include "application/config/RuntimeLayout.hpp"
#include "application/TelegramApplicationService.hpp"
#include "application/TelegramPort.hpp"
#include "domain/approval/ApprovalService.hpp"
#include "domain/policy/PolicyEngine.hpp"
#include "domain/telegram/AuthorizationStateMachine.hpp"
#include "mcp/core/ToolRegistry.hpp"
#include "mcp/v2025_11_25/ProtocolHandler.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>

namespace {

class FakeTelegramService final : public tggate::application::ITelegramService {
public:
    [[nodiscard]] tggate::application::Result<std::vector<tggate::application::Chat>> list_chats(std::string_view) override {
        return std::vector<tggate::application::Chat>{{.id = 42, .title = "Development"}, {.id = 100, .title = "Private"}};
    }
    [[nodiscard]] tggate::application::Result<std::vector<tggate::application::Message>> get_messages(
        std::string_view, std::int64_t chat_id, std::size_t) override {
        return std::vector<tggate::application::Message>{{.id = 1, .chat_id = chat_id, .text = "External text"}};
    }
    [[nodiscard]] tggate::application::Result<tggate::application::Message> send_message(
        std::string_view, std::int64_t chat_id, std::string_view text) override {
        return tggate::application::Message{.id = 7, .chat_id = chat_id, .text = std::string(text)};
    }
};

[[nodiscard]] tggate::domain::policy::McpClientProfile profile() {
    return {
        .id = "test-client",
        .display_name = "Test client",
        .allowed_accounts = {"work"},
        .allowed_tools = {"telegram_list_chats", "telegram_get_messages", "telegram_prepare_send_message", "telegram_execute_approved_action"},
        .allowed_chats = {42},
        .write_policy = tggate::domain::policy::WritePolicy::require_approval,
    };
}

} // namespace

int main() {
    using namespace tggate;

    domain::policy::PolicyEngine policy;
    const auto client = profile();
    assert(policy.evaluate(client, {.tool_name = "telegram_get_messages", .account_id = "work", .chat_id = 42}).is_allowed());
    assert(!policy.evaluate(client, {.tool_name = "telegram_get_messages", .account_id = "work", .chat_id = 100}).is_allowed());
    assert(policy.evaluate(client, {.tool_name = "telegram_prepare_send_message", .account_id = "work", .chat_id = 42, .is_write = true}).effect
        == domain::policy::PolicyEffect::require_approval);
    assert(!policy.evaluate(client, {.tool_name = "telegram_get_messages", .account_id = "private", .chat_id = 42}).is_allowed());

    domain::telegram::AuthorizationStateMachine authorization;
    std::string authorization_error;
    const auto generation = authorization.begin("work");
    authorization.observe(generation, domain::telegram::AuthorizationState::wait_code);
    assert(authorization.begin_input(generation, domain::telegram::AuthorizationInput::code, authorization_error));
    assert(!authorization.begin_input(generation, domain::telegram::AuthorizationInput::code, authorization_error));
    authorization.observe(generation, domain::telegram::AuthorizationState::wait_password);
    assert(!authorization.begin_input(generation - 1, domain::telegram::AuthorizationInput::password, authorization_error));
    assert(authorization.begin_input(generation, domain::telegram::AuthorizationInput::password, authorization_error));

    FakeTelegramService telegram;
    domain::approval::ApprovalService approvals;
    application::AuditService audit;
    application::TelegramApplicationService service(telegram, policy, approvals, audit);

    const auto chats = service.list_chats(client, "work");
    assert(chats.at("ok") && chats.at("chats").size() == 1 && chats.at("chats").at(0).at("id") == 42);

    const auto prepared = service.prepare_send_message(client, "work", 42, "hello");
    assert(prepared.at("ok") && prepared.at("status") == "pending_local_approval");
    const auto action_id = prepared.at("action_id").get<std::string>();
    assert(!service.execute_approved_action(client, action_id).at("ok"));
    assert(approvals.approve(action_id).has_value());
    assert(service.execute_approved_action(client, action_id).at("ok"));
    assert(!service.execute_approved_action(client, action_id).at("ok"));

    mcp::core::ToolRegistry tools;
    tools.add({"telegram_get_messages", "Read an allowlisted chat", mcp::core::ToolSurface::read, nlohmann::json::object()});
    tools.add({"telegram_prepare_send_message", "Prepare a message", mcp::core::ToolSurface::write, nlohmann::json::object()});
    mcp::v2025_11_25::ProtocolHandler read_handler(tools, service, client, mcp::core::ToolSurface::read);
    const auto hidden_write_tool = read_handler.handle({
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
        {"params", {{"name", "telegram_prepare_send_message"}, {"arguments", nlohmann::json::object()}}},
    });
    assert(hidden_write_tool.contains("error"));
    assert(!audit.entries().empty());

    std::vector<domain::policy::McpClientProfile> parsed_profiles;
    std::string configuration_error;
    assert(application::config::ClientProfileRepository::parse({
        {"schema_version", 1}, {"clients", {{{"id", "desktop"}, {"display_name", "Desktop"},
            {"allowed_accounts", {"work"}}, {"allowed_tools", {"telegram_list_chats"}},
            {"allowed_chats", {42}}, {"write_policy", "require_approval"}}}},
    }, parsed_profiles, configuration_error));
    assert(parsed_profiles.size() == 1);
    assert(!application::config::ClientProfileRepository::parse(nlohmann::json::object(), parsed_profiles, configuration_error));

    std::vector<application::config::AccountConfiguration> parsed_accounts;
    assert(application::config::AccountRepository::parse({
        {"schema_version", 1}, {"accounts", {{{"id", "work"}, {"display_name", "Work"}, {"database_directory", "data/tdlib/work/db"},
            {"files_directory", "data/tdlib/work/files"}, {"api_id", 12345}, {"api_hash_dpapi", ""}, {"database_key_dpapi", ""}}}},
    }, parsed_accounts, configuration_error));
    assert(parsed_accounts.size() == 1);
    assert(!application::config::AccountRepository::parse(nlohmann::json::object(), parsed_accounts, configuration_error));

    application::config::McpHostConfiguration host_configuration;
    assert(application::config::McpHostConfigurationRepository::parse({
        {"schema_version", 2}, {"enabled", false}, {"bind_address", "127.0.0.1"}, {"port", 8765},
        {"max_request_body_bytes", 1024}, {"maximum_connections", 2}, {"allowed_origins", nlohmann::json::array()},
        {"client_credentials", nlohmann::json::array()},
    }, host_configuration, configuration_error));
    assert(application::config::McpHostConfigurationRepository::parse({
        {"schema_version", 1}, {"enabled", false}, {"bind_address", "127.0.0.1"}, {"port", 8765},
        {"max_request_body_bytes", 1024}, {"maximum_connections", 2}, {"allowed_origins", nlohmann::json::array()},
        {"bearer_token_dpapi", ""},
    }, host_configuration, configuration_error));
    assert(!application::config::McpHostConfigurationRepository::parse({
        {"schema_version", 1}, {"bind_address", "0.0.0.0"},
    }, host_configuration, configuration_error));

    const auto runtime_root = std::filesystem::temp_directory_path() / "tggate-policy-test-runtime";
    std::error_code filesystem_error;
    std::filesystem::remove_all(runtime_root, filesystem_error);
    application::config::RuntimeLayout runtime_layout;
    runtime_layout.root = runtime_root;
    runtime_layout.config_directory = runtime_root / "config";
    runtime_layout.tdlib_directory = runtime_root / "tdlib";
    runtime_layout.client_profiles = runtime_root / "config/client-profiles.json";
    runtime_layout.accounts = runtime_root / "config/accounts.json";
    runtime_layout.mcp_host = runtime_root / "config/mcp-host.json";
    assert(application::config::RuntimeLayoutBootstrap::ensure(runtime_layout, configuration_error));
    assert(std::filesystem::exists(runtime_layout.client_profiles));
    assert(std::filesystem::exists(runtime_layout.accounts));
    assert(std::filesystem::exists(runtime_layout.mcp_host));
    std::filesystem::remove_all(runtime_root, filesystem_error);
    std::cout << "TgGate policy tests passed\n";
}
