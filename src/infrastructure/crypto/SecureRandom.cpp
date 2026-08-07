#include "infrastructure/crypto/SecureRandom.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace tggate::infrastructure::crypto {

std::optional<std::vector<unsigned char>> SecureRandom::bytes(const std::size_t count) {
    if (count == 0 || count > static_cast<std::size_t>(ULONG_MAX)) return std::nullopt;
    std::vector<unsigned char> result(count);
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, result.data(), static_cast<ULONG>(result.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return std::nullopt;
    }
    return result;
#else
    return std::nullopt;
#endif
}

} // namespace tggate::infrastructure::crypto
