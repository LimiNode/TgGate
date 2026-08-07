#pragma once

#include "application/TelegramApplicationService.hpp"
#include "domain/policy/Policy.hpp"
#include "mcp/core/ToolRegistry.hpp"

#include <nlohmann/json.hpp>

namespace tggate::mcp::v2026_07_28 {

inline constexpr auto kProtocolVersion = "2026-07-28";

class ProtocolHandler final {
public:
    ProtocolHandler(
        const core::ToolRegistry& tools,
        application::TelegramApplicationService& telegram_service,
        const domain::policy::McpClientProfile& profile);

    [[nodiscard]] static bool supports_method(std::string_view method) noexcept;
    [[nodiscard]] static bool requires_name(std::string_view method) noexcept;
    [[nodiscard]] nlohmann::json handle(const nlohmann::json& request);

private:
    [[nodiscard]] nlohmann::json handle_tool_call(const nlohmann::json& request);
    [[nodiscard]] nlohmann::json result(const nlohmann::json& id, nlohmann::json value) const;
    [[nodiscard]] nlohmann::json rpc_error(const nlohmann::json& id, int code, std::string_view message) const;

    const core::ToolRegistry& tools_;
    application::TelegramApplicationService& telegram_service_;
    const domain::policy::McpClientProfile& profile_;
};

} // namespace tggate::mcp::v2026_07_28
