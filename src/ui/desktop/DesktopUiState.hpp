#pragma once

#include "ui/desktop/DesktopUiPort.hpp"

#include <memory>

namespace tggate::ui::desktop {

class DesktopUiState final : public DesktopUiPort {
public:
    DesktopUiState();
    ~DesktopUiState() override;

    DesktopUiState(const DesktopUiState&) = delete;
    DesktopUiState& operator=(const DesktopUiState&) = delete;

    [[nodiscard]] std::vector<PendingActionRow> pending_actions() const override;
    [[nodiscard]] bool approve(std::string_view action_id) override;
    [[nodiscard]] bool deny(std::string_view action_id) override;
    void lockdown() override;
    [[nodiscard]] std::string server_status() const override;
    [[nodiscard]] std::vector<McpClientCredentialRow> mcp_client_credentials() const override;
    [[nodiscard]] std::optional<std::string> mcp_bearer_token_for_export(std::string_view client_id) const override;
    [[nodiscard]] bool regenerate_mcp_bearer_token(std::string_view client_id) override;
    [[nodiscard]] std::string telegram_status() const override;
    [[nodiscard]] std::size_t configured_client_count() const override;
    [[nodiscard]] std::string configuration_status() const override;
    [[nodiscard]] std::vector<AccountRow> accounts() const override;
    [[nodiscard]] std::string authorization_error() const override;
    [[nodiscard]] bool start_authorization(
        std::string_view account_id, std::string_view phone_number, std::string_view api_hash) override;
    [[nodiscard]] bool submit_authentication_code(std::string_view code) override;
    [[nodiscard]] bool submit_authentication_password(std::string_view password) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tggate::ui::desktop
