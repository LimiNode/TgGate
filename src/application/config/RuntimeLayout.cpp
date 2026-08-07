#include "application/config/RuntimeLayout.hpp"

#include <system_error>

namespace tggate::application::config {
namespace {

bool copy_if_missing(const std::filesystem::path& source, const std::filesystem::path& destination, std::error_code& error) {
    if (std::filesystem::exists(destination)) return true;
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);
    return !error;
}

} // namespace

bool RuntimeLayoutBootstrap::ensure(const RuntimeLayout& layout, std::string& status) {
    std::error_code error;
    std::filesystem::create_directories(layout.config_directory, error);
    if (!error) std::filesystem::create_directories(layout.tdlib_directory, error);
    if (error) {
        status = "Cannot create TgGate data directories: " + error.message();
        return false;
    }
    if (!copy_if_missing(layout.template_directory / "client-profiles.example.json", layout.client_profiles, error)) {
        status = "Cannot stage client profile configuration: " + error.message();
        return false;
    }
    if (!copy_if_missing(layout.template_directory / "accounts.example.json", layout.accounts, error)) {
        status = "Cannot stage account configuration: " + error.message();
        return false;
    }
    if (!copy_if_missing(layout.template_directory / "mcp-host.example.json", layout.mcp_host, error)) {
        status = "Cannot stage MCP host configuration: " + error.message();
        return false;
    }
    status = "Runtime configuration is ready at " + layout.config_directory.string();
    return true;
}

} // namespace tggate::application::config
