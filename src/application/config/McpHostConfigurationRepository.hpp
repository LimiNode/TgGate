#pragma once

#include "application/security/SecretBuffer.hpp"
#include "domain/policy/Policy.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace tggate::application::config {

struct McpClientCredential final {
    std::string client_id;
    std::string bearer_token_dpapi;
};

struct McpHostConfiguration final {
    bool enabled = false;
    std::string bind_address = "127.0.0.1";
    unsigned short port = 8765;
    std::size_t max_request_body_bytes = 1024 * 1024;
    std::size_t maximum_connections = 16;
    std::vector<std::string> allowed_origins;
    std::vector<McpClientCredential> client_credentials;
    // In-memory compatibility input for schema v1. It is never written back;
    // ensure_client_credentials converts it only when one profile is present.
    std::string legacy_bearer_token_dpapi;
};

class McpHostConfigurationRepository final {
public:
    [[nodiscard]] static bool parse(const nlohmann::json& document, McpHostConfiguration& configuration, std::string& error);
    [[nodiscard]] static bool load(const std::filesystem::path& path, McpHostConfiguration& configuration, std::string& error);
    [[nodiscard]] static bool save(const std::filesystem::path& path, const McpHostConfiguration& configuration, std::string& error);
    // Creates credentials only for profiles without one. Existing credentials
    // are verified and never replaced implicitly: a DPAPI failure is fail-closed.
    [[nodiscard]] static bool ensure_client_credentials(
        McpHostConfiguration& configuration,
        const std::vector<domain::policy::McpClientProfile>& profiles,
        std::string& error);
    [[nodiscard]] static bool regenerate_client_credential(
        McpHostConfiguration& configuration,
        std::string_view client_id,
        std::string& error);
    [[nodiscard]] static const McpClientCredential* find_client_credential(
        const McpHostConfiguration& configuration,
        std::string_view client_id) noexcept;
    [[nodiscard]] static std::optional<security::SecretBuffer> unprotect_bearer_token(std::string_view protected_token);
};

} // namespace tggate::application::config
