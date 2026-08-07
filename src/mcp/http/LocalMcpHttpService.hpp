#pragma once

#include "application/config/McpHostConfigurationRepository.hpp"
#include "application/http/IHttpHost.hpp"
#include "application/security/SecretBuffer.hpp"
#include "application/TelegramApplicationService.hpp"
#include "domain/policy/Policy.hpp"
#include "mcp/core/ToolRegistry.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace tggate::mcp::http {

// Owns the local HTTP MCP lifecycle, including configuration, bearer-token
// authentication and client-profile resolution. The concrete HTTP server stays
// behind IHttpHost and has no knowledge of MCP or Telegram.
class LocalMcpHttpService final {
public:
    LocalMcpHttpService(
        application::http::IHttpHost& host,
        const core::ToolRegistry& tools,
        application::TelegramApplicationService& telegram_service);

    LocalMcpHttpService(const LocalMcpHttpService&) = delete;
    LocalMcpHttpService& operator=(const LocalMcpHttpService&) = delete;
    ~LocalMcpHttpService();

    // Loads per-client DPAPI credentials, verifies existing credentials without
    // regenerating them, then starts the host if the configuration is enabled.
    [[nodiscard]] bool start(
        const std::filesystem::path& configuration_path,
        std::vector<domain::policy::McpClientProfile> profiles,
        std::string& error);
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] const std::string& status() const noexcept;

private:
    struct AuthenticatedClient final {
        domain::policy::McpClientProfile profile;
        application::security::SecretBuffer bearer_token;
    };

    [[nodiscard]] application::http::HttpResponse handle(const application::http::HttpRequest& request);

    application::http::IHttpHost& host_;
    const core::ToolRegistry& tools_;
    application::TelegramApplicationService& telegram_service_;
    application::config::McpHostConfiguration configuration_;
    std::vector<AuthenticatedClient> clients_;
    bool running_ = false;
    std::string status_ = "MCP host is not started";
};

} // namespace tggate::mcp::http
