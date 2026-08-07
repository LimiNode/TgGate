#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tggate::mcp::core {

enum class ToolSurface { read, write };

struct ToolDefinition {
    std::string name;
    std::string description;
    ToolSurface surface;
    nlohmann::json input_schema;
};

class ToolRegistry final {
public:
    void add(ToolDefinition definition);
    [[nodiscard]] const ToolDefinition* find(std::string_view name, ToolSurface surface) const;
    [[nodiscard]] const ToolDefinition* find(std::string_view name) const;
    [[nodiscard]] nlohmann::json list(ToolSurface surface) const;
    [[nodiscard]] nlohmann::json list() const;

private:
    std::vector<ToolDefinition> tools_;
};

} // namespace tggate::mcp::core
