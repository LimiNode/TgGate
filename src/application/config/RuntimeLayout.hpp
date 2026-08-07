#pragma once

#include <filesystem>
#include <string>

namespace tggate::application::config {

struct RuntimeLayout final {
    std::filesystem::path root = "data";
    std::filesystem::path config_directory = root / "config";
    std::filesystem::path tdlib_directory = root / "tdlib";
    std::filesystem::path client_profiles = config_directory / "client-profiles.json";
    std::filesystem::path accounts = config_directory / "accounts.json";
    std::filesystem::path mcp_host = config_directory / "mcp-host.json";
#ifdef TGGATE_CONFIG_TEMPLATE_DIR
    std::filesystem::path template_directory = TGGATE_CONFIG_TEMPLATE_DIR;
#else
    std::filesystem::path template_directory = "configs";
#endif
};

class RuntimeLayoutBootstrap final {
public:
    // Creates only missing directories/files. Existing user configuration is
    // never replaced; its validation remains the repository's responsibility.
    [[nodiscard]] static bool ensure(const RuntimeLayout& layout, std::string& status);
};

} // namespace tggate::application::config
