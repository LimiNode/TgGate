#include "infrastructure/dpapi/DpapiProtector.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace tggate::infrastructure::dpapi {
namespace {

#ifdef _WIN32
std::optional<std::vector<unsigned char>> crypt(
    const std::vector<unsigned char>& input,
    const std::string_view purpose,
    const bool protect) {
    DATA_BLOB data{.cbData = static_cast<DWORD>(input.size()), .pbData = const_cast<BYTE*>(input.data())};
    DATA_BLOB entropy{.cbData = static_cast<DWORD>(purpose.size()), .pbData = reinterpret_cast<BYTE*>(const_cast<char*>(purpose.data()))};
    DATA_BLOB output{};
    const auto success = protect
        ? CryptProtectData(&data, L"TgGate", purpose.empty() ? nullptr : &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
        : CryptUnprotectData(&data, nullptr, purpose.empty() ? nullptr : &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!success) return std::nullopt;
    std::vector<unsigned char> result(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return result;
}
#endif

} // namespace

std::optional<std::vector<unsigned char>> DpapiProtector::protect(
    const std::vector<unsigned char>& plain_text,
    const std::string_view purpose) {
#ifdef _WIN32
    return crypt(plain_text, purpose, true);
#else
    static_cast<void>(plain_text); static_cast<void>(purpose);
    return std::nullopt;
#endif
}

std::optional<std::vector<unsigned char>> DpapiProtector::unprotect(
    const std::vector<unsigned char>& cipher_text,
    const std::string_view purpose) {
#ifdef _WIN32
    return crypt(cipher_text, purpose, false);
#else
    static_cast<void>(cipher_text); static_cast<void>(purpose);
    return std::nullopt;
#endif
}

} // namespace tggate::infrastructure::dpapi
