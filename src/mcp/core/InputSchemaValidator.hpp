#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace tggate::mcp::core {

// Validates the intentionally small JSON Schema subset used by TgGate tool
// definitions. The validator is shared by every protocol revision so a schema
// advertised through tools/list is also enforced before dispatch.
[[nodiscard]] std::optional<std::string> validate_tool_arguments(
    const nlohmann::json& input_schema, const nlohmann::json& arguments);

} // namespace tggate::mcp::core
