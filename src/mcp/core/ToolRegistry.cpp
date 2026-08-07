#include "mcp/core/ToolRegistry.hpp"

#include <ranges>

namespace tggate::mcp::core {

void ToolRegistry::add(ToolDefinition definition) {
    tools_.push_back(std::move(definition));
}

const ToolDefinition* ToolRegistry::find(const std::string_view name, const ToolSurface surface) const {
    const auto found = std::ranges::find_if(tools_, [name, surface](const ToolDefinition& tool) {
        return tool.name == name && tool.surface == surface;
    });
    return found == tools_.end() ? nullptr : &*found;
}

const ToolDefinition* ToolRegistry::find(const std::string_view name) const {
    const auto found = std::ranges::find_if(tools_, [name](const ToolDefinition& tool) {
        return tool.name == name;
    });
    return found == tools_.end() ? nullptr : &*found;
}

nlohmann::json ToolRegistry::list(const ToolSurface surface) const {
    auto result = nlohmann::json::array();
    for (const auto& tool : tools_) {
        if (tool.surface == surface) {
            result.push_back({{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.input_schema}});
        }
    }
    return result;
}

nlohmann::json ToolRegistry::list() const {
    auto result = nlohmann::json::array();
    for (const auto& tool : tools_) {
        result.push_back({{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.input_schema}});
    }
    return result;
}

} // namespace tggate::mcp::core
