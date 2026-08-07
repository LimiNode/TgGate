#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace tggate::infrastructure::crypto {

class SecureRandom final {
public:
    [[nodiscard]] static std::optional<std::vector<unsigned char>> bytes(std::size_t count);
};

} // namespace tggate::infrastructure::crypto
