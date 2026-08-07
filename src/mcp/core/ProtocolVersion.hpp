#pragma once

#include <string_view>

namespace tggate::mcp::core {

enum class ProtocolVersion { v2025_11_25, v2026_07_28 };

[[nodiscard]] constexpr std::string_view to_string(const ProtocolVersion version) {
    switch (version) {
    case ProtocolVersion::v2025_11_25: return "2025-11-25";
    case ProtocolVersion::v2026_07_28: return "2026-07-28";
    }
    return "unknown";
}

} // namespace tggate::mcp::core
