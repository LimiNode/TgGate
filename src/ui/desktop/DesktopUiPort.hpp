#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tggate::ui::desktop {

struct PendingActionRow {
    std::string id;
    std::string client_id;
    std::string account_id;
    std::string tool_name;
    std::string recipient;
    std::string text;
};

struct AccountRow {
    std::string id;
    std::string display_name;
    std::string authorization_status;
    bool has_api_credentials = false;
};

struct McpClientCredentialRow {
    std::string id;
    std::string display_name;
    bool enabled = false;
};

class DesktopUiPort {
public:
    virtual ~DesktopUiPort() = default;

    [[nodiscard]] virtual std::vector<PendingActionRow> pending_actions() const = 0;
    [[nodiscard]] virtual bool approve(std::string_view action_id) = 0;
    [[nodiscard]] virtual bool deny(std::string_view action_id) = 0;
    virtual void lockdown() = 0;
    [[nodiscard]] virtual std::string server_status() const = 0;
    // Explicit operator action only. The returned value is never persisted by
    // the UI and is intended solely for copying to a local MCP client.
    [[nodiscard]] virtual std::vector<McpClientCredentialRow> mcp_client_credentials() const = 0;
    [[nodiscard]] virtual std::optional<std::string> mcp_bearer_token_for_export(std::string_view client_id) const = 0;
    [[nodiscard]] virtual bool regenerate_mcp_bearer_token(std::string_view client_id) = 0;
    [[nodiscard]] virtual std::string telegram_status() const = 0;
    [[nodiscard]] virtual std::size_t configured_client_count() const = 0;
    [[nodiscard]] virtual std::string configuration_status() const = 0;
    [[nodiscard]] virtual std::vector<AccountRow> accounts() const = 0;
    [[nodiscard]] virtual std::string authorization_error() const = 0;
    [[nodiscard]] virtual bool start_authorization(
        std::string_view account_id, std::string_view phone_number, std::string_view api_hash) = 0;
    [[nodiscard]] virtual bool submit_authentication_code(std::string_view code) = 0;
    [[nodiscard]] virtual bool submit_authentication_password(std::string_view password) = 0;
};

} // namespace tggate::ui::desktop
