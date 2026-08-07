#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace tggate::infrastructure::dpapi {

class DpapiProtector final {
public:
    [[nodiscard]] static std::optional<std::vector<unsigned char>> protect(
        const std::vector<unsigned char>& plain_text,
        std::string_view purpose);
    [[nodiscard]] static std::optional<std::vector<unsigned char>> unprotect(
        const std::vector<unsigned char>& cipher_text,
        std::string_view purpose);
};

} // namespace tggate::infrastructure::dpapi
