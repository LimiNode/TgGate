#include "application/config/AccountRepository.hpp"

#include "infrastructure/dpapi/DpapiProtector.hpp"
#include "infrastructure/crypto/SecureRandom.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace tggate::application::config {
namespace {

std::string dpapi_purpose(const std::string_view account_id) {
    return "tggate/account/" + std::string(account_id) + "/api_hash";
}

std::string database_key_dpapi_purpose(const std::string_view account_id) {
    return "tggate/account/" + std::string(account_id) + "/database_key";
}

void wipe_bytes(std::vector<unsigned char>& value) noexcept {
    if (value.empty()) return;
#ifdef _WIN32
    SecureZeroMemory(value.data(), value.size());
#else
    volatile unsigned char* current = value.data();
    for (std::size_t index = 0; index < value.size(); ++index) current[index] = 0;
#endif
    value.clear();
}

class WipeBytesOnExit final {
public:
    explicit WipeBytesOnExit(std::vector<unsigned char>& value) noexcept : value_(value) {}
    ~WipeBytesOnExit() noexcept { wipe_bytes(value_); }
    WipeBytesOnExit(const WipeBytesOnExit&) = delete;
    WipeBytesOnExit& operator=(const WipeBytesOnExit&) = delete;

private:
    std::vector<unsigned char>& value_;
};

std::string hex_encode(const std::vector<unsigned char>& bytes) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto value : bytes) stream << std::setw(2) << static_cast<unsigned int>(value);
    return stream.str();
}

std::optional<std::vector<unsigned char>> hex_decode(const std::string_view value) {
    if (value.empty() || value.size() % 2 != 0) return std::nullopt;
    std::vector<unsigned char> result;
    result.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        unsigned int byte = 0;
        std::istringstream stream(std::string(value.substr(index, 2)));
        stream >> std::hex >> byte;
        if (stream.fail() || byte > 0xff) return std::nullopt;
        result.push_back(static_cast<unsigned char>(byte));
    }
    return result;
}

bool is_safe_tdlib_data_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) return false;
    const auto normalized = path.lexically_normal();
    if (normalized.empty() || normalized == ".") return false;
    for (const auto& component : normalized) {
        if (component == "..") return false;
    }
    const auto iterator = normalized.begin();
    return iterator != normalized.end() && *iterator == "data";
}

bool has_only_account_fields(const nlohmann::json& value) {
    static const std::unordered_set<std::string> fields{
        "id", "display_name", "database_directory", "files_directory", "api_id", "api_hash_dpapi", "database_key_dpapi"};
    for (const auto& [key, ignored] : value.items()) {
        static_cast<void>(ignored);
        if (!fields.contains(key)) return false;
    }
    return true;
}

} // namespace

bool AccountRepository::parse(
    const nlohmann::json& document,
    std::vector<AccountConfiguration>& accounts,
    std::string& error) {
    if (!document.is_object() || document.value("schema_version", 0) != 1 || !document.contains("accounts") || !document.at("accounts").is_array()) {
        error = "Account configuration requires schema_version 1 and an accounts array";
        return false;
    }

    static const std::unordered_set<std::string> root_fields{"schema_version", "accounts"};
    for (const auto& [key, ignored] : document.items()) {
        static_cast<void>(ignored);
        if (!root_fields.contains(key)) {
            error = "Unknown field in account configuration";
            return false;
        }
    }

    std::unordered_set<std::string> identifiers;
    std::unordered_set<std::string> data_directories;
    std::vector<AccountConfiguration> parsed;
    try {
        for (const auto& value : document.at("accounts")) {
            if (!value.is_object() || value.contains("api_hash") || !has_only_account_fields(value)) {
                error = "Account contains an unsupported or plaintext secret field";
                return false;
            }
            AccountConfiguration account{
                .id = value.at("id").get<std::string>(),
                .display_name = value.at("display_name").get<std::string>(),
                .database_directory = value.at("database_directory").get<std::string>(),
                .files_directory = value.at("files_directory").get<std::string>(),
                .api_id = value.value("api_id", 0),
                .api_hash_dpapi = value.value("api_hash_dpapi", ""),
                .database_key_dpapi = value.value("database_key_dpapi", ""),
            };
            if (account.id.empty() || !identifiers.insert(account.id).second || account.database_directory.empty() ||
                account.files_directory.empty() || account.api_id < 0 || !is_safe_tdlib_data_path(account.database_directory) ||
                !is_safe_tdlib_data_path(account.files_directory) || !data_directories.insert(account.database_directory.generic_string()).second ||
                !data_directories.insert(account.files_directory.generic_string()).second) {
                error = "Every account needs unique safe data directories and a non-negative api_id";
                return false;
            }
            parsed.push_back(std::move(account));
        }
    } catch (const nlohmann::json::exception&) {
        error = "Invalid account configuration field type";
        return false;
    }
    accounts = std::move(parsed);
    return true;
}

bool AccountRepository::load(
    const std::filesystem::path& path,
    std::vector<AccountConfiguration>& accounts,
    std::string& error) {
    std::ifstream stream(path);
    if (!stream) {
        error = "Cannot open account configuration file";
        return false;
    }
    try {
        return parse(nlohmann::json::parse(stream), accounts, error);
    } catch (const nlohmann::json::exception&) {
        error = "Account configuration file is not valid JSON";
        return false;
    }
}

bool AccountRepository::save(
    const std::filesystem::path& path,
    const std::vector<AccountConfiguration>& accounts,
    std::string& error) {
    nlohmann::json document{{"schema_version", 1}, {"accounts", nlohmann::json::array()}};
    for (const auto& account : accounts) {
        document["accounts"].push_back({
            {"id", account.id}, {"display_name", account.display_name},
            {"database_directory", account.database_directory.generic_string()},
            {"files_directory", account.files_directory.generic_string()}, {"api_id", account.api_id},
            {"api_hash_dpapi", account.api_hash_dpapi}, {"database_key_dpapi", account.database_key_dpapi},
        });
    }
    const auto temporary_path = path.string() + ".tmp";
    {
        std::ofstream stream(temporary_path, std::ios::trunc);
        if (!stream) {
            error = "Cannot write temporary account configuration";
            return false;
        }
        stream << document.dump(2) << '\n';
        if (!stream) {
            error = "Cannot finish writing account configuration";
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(std::filesystem::path(temporary_path).c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot atomically replace account configuration";
        return false;
    }
#else
    std::error_code filesystem_error;
    std::filesystem::rename(temporary_path, path, filesystem_error);
    if (filesystem_error) {
        error = "Cannot atomically replace account configuration: " + filesystem_error.message();
        return false;
    }
#endif
    return true;
}

std::optional<std::string> AccountRepository::protect_api_hash(
    const std::string_view account_id,
    const std::string_view api_hash) {
    if (account_id.empty() || api_hash.empty()) return std::nullopt;
    std::vector<unsigned char> input(api_hash.begin(), api_hash.end());
    const WipeBytesOnExit wipe_input(input);
    const auto protected_bytes = infrastructure::dpapi::DpapiProtector::protect(input, dpapi_purpose(account_id));
    if (!protected_bytes) return std::nullopt;
    return hex_encode(*protected_bytes);
}

std::optional<security::SecretBuffer> AccountRepository::unprotect_api_hash(
    const std::string_view account_id,
    const std::string_view api_hash_dpapi) {
    if (account_id.empty()) return std::nullopt;
    const auto protected_bytes = hex_decode(api_hash_dpapi);
    if (!protected_bytes) return std::nullopt;
    const auto plain_bytes = infrastructure::dpapi::DpapiProtector::unprotect(*protected_bytes, dpapi_purpose(account_id));
    if (!plain_bytes) return std::nullopt;
    return security::SecretBuffer(std::move(*plain_bytes));
}

bool AccountRepository::ensure_database_key(AccountConfiguration& account, std::string& error) {
    if (account.id.empty()) {
        error = "Cannot create a database key for an account without an id";
        return false;
    }
    if (!account.database_key_dpapi.empty()) {
        if (!unprotect_database_key(account.id, account.database_key_dpapi)) {
            error = "TDLib database key cannot be decrypted for this Windows user and computer";
            return false;
        }
        return true;
    }
    if (std::filesystem::exists(account.database_directory)) {
        error = "TDLib database directory already exists but its protected database key is absent";
        return false;
    }
    auto key = infrastructure::crypto::SecureRandom::bytes(32);
    if (!key) {
        error = "Windows CSPRNG failed while creating the TDLib database key";
        return false;
    }
    const WipeBytesOnExit wipe_key(*key);
    const auto protected_key = infrastructure::dpapi::DpapiProtector::protect(*key, database_key_dpapi_purpose(account.id));
    if (!protected_key) {
        error = "Windows DPAPI failed while protecting the TDLib database key";
        return false;
    }
    account.database_key_dpapi = hex_encode(*protected_key);
    return true;
}

std::optional<security::SecretBuffer> AccountRepository::unprotect_database_key(
    const std::string_view account_id,
    const std::string_view database_key_dpapi) {
    if (account_id.empty()) return std::nullopt;
    const auto protected_bytes = hex_decode(database_key_dpapi);
    if (!protected_bytes) return std::nullopt;
    const auto plain_bytes = infrastructure::dpapi::DpapiProtector::unprotect(*protected_bytes, database_key_dpapi_purpose(account_id));
    if (!plain_bytes || plain_bytes->size() != 32) return std::nullopt;
    return security::SecretBuffer(std::move(*plain_bytes));
}

} // namespace tggate::application::config
