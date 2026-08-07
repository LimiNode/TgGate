#pragma once

#include "application/security/SecretBuffer.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace tggate::application::config {

// The API hash is never represented in this structure in plain text. Its
// DPAPI-protected byte representation belongs in api_hash_dpapi.
struct AccountConfiguration final {
    std::string id;
    std::string display_name;
    std::filesystem::path database_directory;
    std::filesystem::path files_directory;
    int api_id = 0;
    std::string api_hash_dpapi;
    std::string database_key_dpapi;
};

class AccountRepository final {
public:
    [[nodiscard]] static bool parse(
        const nlohmann::json& document,
        std::vector<AccountConfiguration>& accounts,
        std::string& error);
    [[nodiscard]] static bool load(
        const std::filesystem::path& path,
        std::vector<AccountConfiguration>& accounts,
        std::string& error);
    [[nodiscard]] static bool save(
        const std::filesystem::path& path,
        const std::vector<AccountConfiguration>& accounts,
        std::string& error);

    [[nodiscard]] static std::optional<std::string> protect_api_hash(
        std::string_view account_id,
        std::string_view api_hash);
    [[nodiscard]] static std::optional<std::string> unprotect_api_hash(
        std::string_view account_id,
        std::string_view api_hash_dpapi);
    [[nodiscard]] static bool ensure_database_key(AccountConfiguration& account, std::string& error);
    [[nodiscard]] static std::optional<security::SecretBuffer> unprotect_database_key(
        std::string_view account_id,
        std::string_view database_key_dpapi);
};

} // namespace tggate::application::config
