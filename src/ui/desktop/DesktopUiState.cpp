#include "ui/desktop/DesktopUiState.hpp"

#include "application/config/ClientProfileRepository.hpp"
#include "application/config/AccountRepository.hpp"
#include "application/config/McpHostConfigurationRepository.hpp"
#include "application/config/RuntimeLayout.hpp"
#include "application/AuditService.hpp"
#include "application/TelegramApplicationService.hpp"
#include "application/TelegramPort.hpp"
#include "domain/approval/ApprovalService.hpp"
#include "domain/policy/PolicyEngine.hpp"
#include "ui/desktop/PendingActionsViewModel.hpp"

#include <filesystem>
#include <algorithm>

#ifdef TGGATE_WITH_HTTP_HOST
#include "infrastructure/http/SimpleWebHttpHost.hpp"
#include "mcp/http/LocalMcpHttpService.hpp"
#endif

#ifdef TGGATE_WITH_TDLIB
#include "infrastructure/tdlib/TdAccount.hpp"
#endif

namespace tggate::ui::desktop {
namespace {

void add_default_tools(mcp::core::ToolRegistry& tools) {
    tools.add({"telegram_list_chats", "List allowlisted Telegram chats", mcp::core::ToolSurface::read, nlohmann::json::object()});
    tools.add({"telegram_get_messages", "Read messages from an allowlisted Telegram chat", mcp::core::ToolSurface::read, nlohmann::json::object()});
    tools.add({"telegram_prepare_send_message", "Prepare a Telegram message for approval", mcp::core::ToolSurface::write, nlohmann::json::object()});
    tools.add({"telegram_execute_approved_action", "Execute an approved Telegram write", mcp::core::ToolSurface::write, nlohmann::json::object()});
}

} // namespace

class DesktopUiState::Impl final {
public:
    Impl()
        : telegram_application(telegram, policy, approvals, audit), pending_actions(approvals)
#ifdef TGGATE_WITH_HTTP_HOST
        , mcp_host(http_host, tools, telegram_application)
#endif
    {
        add_default_tools(tools);
        configuration_ready = application::config::RuntimeLayoutBootstrap::ensure(layout, configuration_status);
        if (!configuration_ready) return;
        config_loaded = application::config::ClientProfileRepository::load(layout.client_profiles, profiles, config_error);
        accounts_loaded = application::config::AccountRepository::load(layout.accounts, account_configurations, account_error);
#ifdef TGGATE_WITH_HTTP_HOST
        if (config_loaded) {
            std::string host_error;
            if (!mcp_host.start(layout.mcp_host, profiles, host_error)) config_error = host_error;
        }
#endif
    }

    application::UnavailableTelegramService telegram;
    domain::policy::PolicyEngine policy;
    domain::approval::ApprovalService approvals;
    application::AuditService audit;
    application::TelegramApplicationService telegram_application;
    PendingActionsViewModel pending_actions;
    mcp::core::ToolRegistry tools;
#ifdef TGGATE_WITH_HTTP_HOST
    infrastructure::http::SimpleWebHttpHost http_host;
    mcp::http::LocalMcpHttpService mcp_host;
#endif
    std::vector<domain::policy::McpClientProfile> profiles;
    application::config::RuntimeLayout layout;
    std::vector<application::config::AccountConfiguration> account_configurations;
    bool config_loaded = false;
    bool accounts_loaded = false;
    bool configuration_ready = false;
    std::string config_error;
    std::string account_error;
    std::string configuration_status;
    std::string auth_error = "TDLib integration is not enabled in this build";
#ifdef TGGATE_WITH_TDLIB
    std::unique_ptr<infrastructure::tdlib::TdAccount> td_account;
    std::string active_account_id;
#endif
    bool lockdown_active = false;
};

DesktopUiState::DesktopUiState() : impl_(std::make_unique<Impl>()) {}
DesktopUiState::~DesktopUiState() = default;

std::vector<PendingActionRow> DesktopUiState::pending_actions() const { return impl_->pending_actions.rows(); }
bool DesktopUiState::approve(const std::string_view action_id) { return impl_->pending_actions.approve(action_id); }
bool DesktopUiState::deny(const std::string_view action_id) { return impl_->pending_actions.deny(action_id); }
void DesktopUiState::lockdown() { impl_->pending_actions.lockdown(); impl_->lockdown_active = true; }

std::string DesktopUiState::server_status() const {
#ifdef TGGATE_WITH_HTTP_HOST
    return impl_->lockdown_active ? "LOCKDOWN — all write approvals are denied" : impl_->mcp_host.status();
#else
    return impl_->lockdown_active ? "LOCKDOWN — all write approvals are denied" : "Local MCP host is not started";
#endif
}

std::vector<McpClientCredentialRow> DesktopUiState::mcp_client_credentials() const {
    std::vector<McpClientCredentialRow> rows;
    rows.reserve(impl_->profiles.size());
    for (const auto& profile : impl_->profiles) rows.push_back({.id = profile.id, .display_name = profile.display_name, .enabled = profile.enabled});
    return rows;
}

std::optional<std::string> DesktopUiState::mcp_bearer_token_for_export(const std::string_view client_id) const {
#ifdef TGGATE_WITH_HTTP_HOST
    application::config::McpHostConfiguration configuration;
    std::string error;
    if (!application::config::McpHostConfigurationRepository::load(impl_->layout.mcp_host, configuration, error)) return std::nullopt;
    const auto credential = application::config::McpHostConfigurationRepository::find_client_credential(configuration, client_id);
    if (!credential) return std::nullopt;
    const auto token = application::config::McpHostConfigurationRepository::unprotect_bearer_token(credential->bearer_token_dpapi);
    return token ? std::optional<std::string>(std::string(token->view())) : std::nullopt;
#else
    static_cast<void>(client_id);
    return std::nullopt;
#endif
}

bool DesktopUiState::regenerate_mcp_bearer_token(const std::string_view client_id) {
#ifdef TGGATE_WITH_HTTP_HOST
    application::config::McpHostConfiguration configuration;
    std::string error;
    if (!application::config::McpHostConfigurationRepository::load(impl_->layout.mcp_host, configuration, error) ||
        !application::config::McpHostConfigurationRepository::ensure_client_credentials(configuration, impl_->profiles, error) ||
        !application::config::McpHostConfigurationRepository::regenerate_client_credential(configuration, client_id, error) ||
        !application::config::McpHostConfigurationRepository::save(impl_->layout.mcp_host, configuration, error)) {
        impl_->config_error = error;
        return false;
    }
    if (impl_->mcp_host.running() && !impl_->mcp_host.start(impl_->layout.mcp_host, impl_->profiles, error)) {
        impl_->config_error = error;
        return false;
    }
    return true;
#else
    static_cast<void>(client_id);
    return false;
#endif
}

std::string DesktopUiState::telegram_status() const {
#ifdef TGGATE_WITH_TDLIB
    if (impl_->td_account) return impl_->td_account->authorization_status();
#endif
    return "TDLib account is not configured";
}
std::size_t DesktopUiState::configured_client_count() const { return impl_->profiles.size(); }
std::string DesktopUiState::configuration_status() const {
    if (!impl_->configuration_ready) return impl_->configuration_status;
    if (!impl_->config_loaded) return impl_->config_error;
    if (!impl_->accounts_loaded) return impl_->account_error;
    return impl_->configuration_status;
}

std::vector<AccountRow> DesktopUiState::accounts() const {
    std::vector<AccountRow> result;
    result.reserve(impl_->account_configurations.size());
    for (const auto& account : impl_->account_configurations) {
#ifdef TGGATE_WITH_TDLIB
        const auto status = impl_->td_account && account.id == impl_->active_account_id
            ? impl_->td_account->authorization_status()
            : "Not started";
#else
        const auto status = account.api_id > 0 && !account.api_hash_dpapi.empty()
            ? std::string("Credentials configured; TDLib is disabled")
            : std::string("API credentials are required");
#endif
        result.push_back({
            .id = account.id,
            .display_name = account.display_name,
            .authorization_status = status,
            .has_api_credentials = account.api_id > 0 && !account.api_hash_dpapi.empty(),
        });
    }
    return result;
}

std::string DesktopUiState::authorization_error() const { return impl_->auth_error; }

bool DesktopUiState::start_authorization(std::string_view account_id, std::string_view phone_number, std::string_view api_hash) {
#ifdef TGGATE_WITH_TDLIB
    const auto account = std::find_if(impl_->account_configurations.begin(), impl_->account_configurations.end(),
        [account_id](const application::config::AccountConfiguration& value) { return value.id == account_id; });
    if (account == impl_->account_configurations.end()) {
        impl_->auth_error = "Unknown account";
        return false;
    }
    application::security::SecretBuffer plain_api_hash;
    if (!api_hash.empty()) {
        const auto protected_hash = application::config::AccountRepository::protect_api_hash(account_id, api_hash);
        if (!protected_hash) {
            impl_->auth_error = "Cannot protect API hash with Windows DPAPI";
            return false;
        }
        account->api_hash_dpapi = *protected_hash;
        plain_api_hash = application::security::SecretBuffer(api_hash);
    } else {
        const auto unprotected_hash = application::config::AccountRepository::unprotect_api_hash(account_id, account->api_hash_dpapi);
        if (!unprotected_hash) {
            impl_->auth_error = "Enter the API hash once; it will be protected with DPAPI for later use";
            return false;
        }
        plain_api_hash = std::move(*unprotected_hash);
    }
    if (!application::config::AccountRepository::ensure_database_key(*account, impl_->auth_error)) {
        return false;
    }
    if (!application::config::AccountRepository::save(impl_->layout.accounts, impl_->account_configurations, impl_->auth_error)) {
        return false;
    }
    const auto database_key = application::config::AccountRepository::unprotect_database_key(account_id, account->database_key_dpapi);
    if (!database_key) {
        impl_->auth_error = "TDLib database key cannot be decrypted for this Windows user and computer";
        return false;
    }
    impl_->td_account = std::make_unique<infrastructure::tdlib::TdAccount>();
    impl_->active_account_id = account->id;
    const auto started = impl_->td_account->begin_authorization({
        .account_id = account->id,
        .database_directory = account->database_directory,
        .files_directory = account->files_directory,
        .api_id = account->api_id,
        .api_hash = std::move(plain_api_hash),
        .database_encryption_key = std::move(*database_key),
        .phone_number = std::string(phone_number),
    });
    impl_->auth_error = impl_->td_account->last_error();
    return started;
#else
    static_cast<void>(account_id); static_cast<void>(phone_number); static_cast<void>(api_hash);
    impl_->auth_error = "TDLib integration is not enabled in this build";
    return false;
#endif
}

bool DesktopUiState::submit_authentication_code(std::string_view code) {
#ifdef TGGATE_WITH_TDLIB
    if (!impl_->td_account) {
        impl_->auth_error = "Start authorization first";
        return false;
    }
    const auto accepted = impl_->td_account->submit_code(std::string(code));
    impl_->auth_error = impl_->td_account->last_error();
    return accepted;
#else
    static_cast<void>(code);
    impl_->auth_error = "TDLib integration is not enabled in this build";
    return false;
#endif
}

bool DesktopUiState::submit_authentication_password(std::string_view password) {
#ifdef TGGATE_WITH_TDLIB
    if (!impl_->td_account) {
        impl_->auth_error = "Start authorization first";
        return false;
    }
    const auto accepted = impl_->td_account->submit_password(std::string(password));
    impl_->auth_error = impl_->td_account->last_error();
    return accepted;
#else
    static_cast<void>(password);
    impl_->auth_error = "TDLib integration is not enabled in this build";
    return false;
#endif
}

} // namespace tggate::ui::desktop
