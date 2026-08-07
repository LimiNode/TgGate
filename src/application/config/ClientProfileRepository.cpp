#include "application/config/ClientProfileRepository.hpp"

#include <fstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace tggate::application::config {
namespace {

std::optional<domain::policy::WritePolicy> parse_write_policy(const std::string_view value) {
    if (value == "deny") return domain::policy::WritePolicy::deny;
    if (value == "require_approval") return domain::policy::WritePolicy::require_approval;
    if (value == "auto_approve") return domain::policy::WritePolicy::auto_approve;
    return std::nullopt;
}

} // namespace

bool ClientProfileRepository::parse(
    const nlohmann::json& document,
    std::vector<domain::policy::McpClientProfile>& profiles,
    std::string& error) {
    if (!document.is_object() || document.value("schema_version", 0) != 1 || !document.contains("clients") || !document.at("clients").is_array()) {
        error = "Client configuration requires schema_version 1 and a clients array";
        return false;
    }

    static const std::unordered_set<std::string> root_fields{"schema_version", "clients"};
    static const std::unordered_set<std::string> client_fields{
        "id", "display_name", "enabled", "allowed_accounts", "allowed_tools", "allowed_chats", "write_policy"};
    for (const auto& [key, ignored] : document.items()) {
        static_cast<void>(ignored);
        if (!root_fields.contains(key)) {
            error = "Unknown field in client configuration";
            return false;
        }
    }

    std::unordered_set<std::string> identifiers;
    std::vector<domain::policy::McpClientProfile> parsed;
    try {
        for (const auto& value : document.at("clients")) {
            if (!value.is_object()) {
                error = "A client profile must be an object";
                return false;
            }
            for (const auto& [key, ignored] : value.items()) {
                static_cast<void>(ignored);
                if (!client_fields.contains(key)) {
                    error = "Unknown field in client profile";
                    return false;
                }
            }
            domain::policy::McpClientProfile profile{
                .id = value.at("id").get<std::string>(),
                .display_name = value.at("display_name").get<std::string>(),
                .enabled = value.value("enabled", true),
                .allowed_accounts = value.value("allowed_accounts", std::vector<std::string>{}),
                .allowed_tools = value.value("allowed_tools", std::vector<std::string>{}),
                .allowed_chats = value.value("allowed_chats", std::vector<std::int64_t>{}),
            };
            const auto write_policy = parse_write_policy(value.value("write_policy", "deny"));
            if (profile.id.empty() || !identifiers.insert(profile.id).second || !write_policy) {
                error = "Every client needs a unique non-empty id and a valid write_policy";
                return false;
            }
            profile.write_policy = *write_policy;
            parsed.push_back(std::move(profile));
        }
    } catch (const nlohmann::json::exception&) {
        error = "Invalid client profile field type";
        return false;
    }

    profiles = std::move(parsed);
    return true;
}

bool ClientProfileRepository::load(
    const std::filesystem::path& path,
    std::vector<domain::policy::McpClientProfile>& profiles,
    std::string& error) {
    std::ifstream stream(path);
    if (!stream) {
        error = "Cannot open client profile file";
        return false;
    }
    try {
        return parse(nlohmann::json::parse(stream), profiles, error);
    } catch (const nlohmann::json::exception&) {
        error = "Client profile file is not valid JSON";
        return false;
    }
}

} // namespace tggate::application::config
