#include "application/config/McpHostConfigurationRepository.hpp"

#include "infrastructure/crypto/SecureRandom.hpp"
#include "infrastructure/dpapi/DpapiProtector.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_set>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace tggate::application::config {
namespace {

constexpr auto kTokenPurpose = "tggate/mcp-host/bearer-token";

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

bool is_loopback(const std::string_view value) {
    return value == "127.0.0.1" || value == "::1";
}

std::optional<std::string> generate_protected_bearer_token() {
    constexpr char hex_characters[] = "0123456789abcdef";

    auto token_bytes = infrastructure::crypto::SecureRandom::bytes(32);
    if (!token_bytes) return std::nullopt;
    const WipeBytesOnExit wipe_token_bytes(*token_bytes);
    std::vector<unsigned char> token(token_bytes->size() * 2);
    const WipeBytesOnExit wipe_token(token);
    for (std::size_t index = 0; index < token_bytes->size(); ++index) {
        const auto byte = (*token_bytes)[index];
        token[index * 2] = static_cast<unsigned char>(hex_characters[byte >> 4]);
        token[index * 2 + 1] = static_cast<unsigned char>(hex_characters[byte & 0x0f]);
    }
    const auto protected_token = infrastructure::dpapi::DpapiProtector::protect(token, kTokenPurpose);
    return protected_token ? std::optional<std::string>(hex_encode(*protected_token)) : std::nullopt;
}

} // namespace

bool McpHostConfigurationRepository::parse(
    const nlohmann::json& document,
    McpHostConfiguration& configuration,
    std::string& error) {
    static const std::unordered_set<std::string> v2_fields{
        "schema_version", "enabled", "bind_address", "port", "max_request_body_bytes", "maximum_connections", "allowed_origins", "client_credentials"};
    static const std::unordered_set<std::string> v1_fields{
        "schema_version", "enabled", "bind_address", "port", "max_request_body_bytes", "maximum_connections", "allowed_origins", "bearer_token_dpapi"};
    if (!document.is_object() || (document.value("schema_version", 0) != 1 && document.value("schema_version", 0) != 2)) {
        error = "MCP host configuration requires schema_version 2 (or legacy schema_version 1)";
        return false;
    }
    const auto schema_version = document.at("schema_version").get<int>();
    for (const auto& [key, ignored] : document.items()) {
        static_cast<void>(ignored);
        if (!(schema_version == 2 ? v2_fields : v1_fields).contains(key) || key == "bearer_token") {
            error = "MCP host configuration contains an unknown or plaintext secret field";
            return false;
        }
    }
    try {
        McpHostConfiguration parsed{
            .enabled = document.value("enabled", false),
            .bind_address = document.value("bind_address", "127.0.0.1"),
            .port = document.value("port", static_cast<unsigned short>(8765)),
            .max_request_body_bytes = document.value("max_request_body_bytes", std::size_t{1024 * 1024}),
            .maximum_connections = document.value("maximum_connections", std::size_t{16}),
            .allowed_origins = document.value("allowed_origins", std::vector<std::string>{}),
        };
        if (!is_loopback(parsed.bind_address) || parsed.port == 0 || parsed.max_request_body_bytes == 0 ||
            parsed.max_request_body_bytes > 16 * 1024 * 1024 || parsed.maximum_connections == 0 || parsed.maximum_connections > 128) {
            error = "MCP host must use loopback, a valid port, and bounded request limits";
            return false;
        }
        if (schema_version == 1) {
            const auto legacy_token = document.value("bearer_token_dpapi", std::string{});
            if (!legacy_token.empty() && !document.at("bearer_token_dpapi").is_string()) {
                error = "Invalid legacy MCP bearer token field type";
                return false;
            }
            parsed.legacy_bearer_token_dpapi = legacy_token;
            configuration = std::move(parsed);
            return true;
        }
        if (!document.contains("client_credentials") || !document.at("client_credentials").is_array()) {
            error = "MCP host configuration requires a client_credentials array";
            return false;
        }
        std::unordered_set<std::string> client_ids;
        for (const auto& value : document.at("client_credentials")) {
            if (!value.is_object() || value.size() != 2 || !value.contains("client_id") || !value.contains("bearer_token_dpapi") ||
                !value.at("client_id").is_string() || !value.at("bearer_token_dpapi").is_string()) {
                error = "MCP client credentials require client_id and bearer_token_dpapi only";
                return false;
            }
            const auto client_id = value.at("client_id").get<std::string>();
            if (client_id.empty() || !client_ids.emplace(client_id).second) {
                error = "MCP client credential ids must be unique and non-empty";
                return false;
            }
            parsed.client_credentials.push_back({.client_id = client_id, .bearer_token_dpapi = value.at("bearer_token_dpapi").get<std::string>()});
        }
        configuration = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        error = "Invalid MCP host configuration field type";
        return false;
    }
}

bool McpHostConfigurationRepository::load(
    const std::filesystem::path& path,
    McpHostConfiguration& configuration,
    std::string& error) {
    std::ifstream stream(path);
    if (!stream) {
        error = "Cannot open MCP host configuration";
        return false;
    }
    try {
        return parse(nlohmann::json::parse(stream), configuration, error);
    } catch (const nlohmann::json::exception&) {
        error = "MCP host configuration is not valid JSON";
        return false;
    }
}

bool McpHostConfigurationRepository::save(
    const std::filesystem::path& path,
    const McpHostConfiguration& configuration,
    std::string& error) {
    auto credentials = nlohmann::json::array();
    for (const auto& credential : configuration.client_credentials) {
        credentials.push_back({{"client_id", credential.client_id}, {"bearer_token_dpapi", credential.bearer_token_dpapi}});
    }
    const nlohmann::json document{{"schema_version", 2}, {"enabled", configuration.enabled}, {"bind_address", configuration.bind_address},
        {"port", configuration.port}, {"max_request_body_bytes", configuration.max_request_body_bytes},
        {"maximum_connections", configuration.maximum_connections}, {"allowed_origins", configuration.allowed_origins},
        {"client_credentials", std::move(credentials)}};
    const auto temporary_path = path.string() + ".tmp";
    {
        std::ofstream stream(temporary_path, std::ios::trunc);
        if (!stream) {
            error = "Cannot write temporary MCP host configuration";
            return false;
        }
        stream << document.dump(2) << '\n';
        if (!stream) {
            error = "Cannot finish writing MCP host configuration";
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(std::filesystem::path(temporary_path).c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot atomically replace MCP host configuration";
        return false;
    }
#else
    std::error_code filesystem_error;
    std::filesystem::rename(temporary_path, path, filesystem_error);
    if (filesystem_error) {
        error = "Cannot atomically replace MCP host configuration: " + filesystem_error.message();
        return false;
    }
#endif
    return true;
}

bool McpHostConfigurationRepository::ensure_client_credentials(
    McpHostConfiguration& configuration,
    const std::vector<domain::policy::McpClientProfile>& profiles,
    std::string& error) {
    if (!configuration.legacy_bearer_token_dpapi.empty()) {
        if (profiles.size() != 1) {
            error = "Legacy MCP configuration has one shared bearer token; configure per-client credentials explicitly";
            return false;
        }
        if (!unprotect_bearer_token(configuration.legacy_bearer_token_dpapi)) {
            error = "Legacy MCP bearer token cannot be decrypted for this Windows user and computer";
            return false;
        }
        configuration.client_credentials.push_back({.client_id = profiles.front().id,
            .bearer_token_dpapi = std::move(configuration.legacy_bearer_token_dpapi)});
        configuration.legacy_bearer_token_dpapi.clear();
    }
    for (const auto& credential : configuration.client_credentials) {
        const auto profile = std::find_if(profiles.begin(), profiles.end(), [&credential](const auto& value) {
            return value.id == credential.client_id;
        });
        if (profile == profiles.end()) {
            error = "MCP credential references an unknown client profile";
            return false;
        }
        if (credential.bearer_token_dpapi.empty() || !unprotect_bearer_token(credential.bearer_token_dpapi)) {
            error = "MCP bearer token cannot be decrypted for this Windows user and computer";
            return false;
        }
    }
    for (const auto& profile : profiles) {
        if (find_client_credential(configuration, profile.id)) continue;
        const auto protected_token = generate_protected_bearer_token();
        if (!protected_token) {
            error = "Windows CSPRNG or DPAPI failed while creating an MCP bearer token";
            return false;
        }
        configuration.client_credentials.push_back({.client_id = profile.id, .bearer_token_dpapi = *protected_token});
    }
    return true;
}

bool McpHostConfigurationRepository::regenerate_client_credential(
    McpHostConfiguration& configuration,
    const std::string_view client_id,
    std::string& error) {
    const auto credential = std::find_if(configuration.client_credentials.begin(), configuration.client_credentials.end(), [client_id](const auto& value) {
        return value.client_id == client_id;
    });
    if (credential == configuration.client_credentials.end()) {
        error = "MCP credential does not exist for this client";
        return false;
    }
    const auto protected_token = generate_protected_bearer_token();
    if (!protected_token) {
        error = "Windows CSPRNG or DPAPI failed while rotating the MCP bearer token";
        return false;
    }
    credential->bearer_token_dpapi = *protected_token;
    return true;
}

const McpClientCredential* McpHostConfigurationRepository::find_client_credential(
    const McpHostConfiguration& configuration,
    const std::string_view client_id) noexcept {
    const auto found = std::find_if(configuration.client_credentials.begin(), configuration.client_credentials.end(), [client_id](const auto& value) {
        return value.client_id == client_id;
    });
    return found == configuration.client_credentials.end() ? nullptr : &*found;
}

std::optional<security::SecretBuffer> McpHostConfigurationRepository::unprotect_bearer_token(const std::string_view protected_token) {
    const auto protected_bytes = hex_decode(protected_token);
    if (!protected_bytes) return std::nullopt;
    auto plain_bytes = infrastructure::dpapi::DpapiProtector::unprotect(*protected_bytes, kTokenPurpose);
    if (!plain_bytes) return std::nullopt;
    const WipeBytesOnExit wipe_plain_bytes(*plain_bytes);
    if (plain_bytes->size() != 64 || !std::all_of(plain_bytes->begin(), plain_bytes->end(), [](const unsigned char value) {
            return std::isxdigit(value) != 0;
        })) return std::nullopt;
    return security::SecretBuffer(std::move(*plain_bytes));
}

} // namespace tggate::application::config
