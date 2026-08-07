#pragma once

#include "domain/policy/Policy.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace tggate::application::config {

class ClientProfileRepository final {
public:
    [[nodiscard]] static bool parse(
        const nlohmann::json& document,
        std::vector<domain::policy::McpClientProfile>& profiles,
        std::string& error);
    [[nodiscard]] static bool load(
        const std::filesystem::path& path,
        std::vector<domain::policy::McpClientProfile>& profiles,
        std::string& error);
};

} // namespace tggate::application::config
