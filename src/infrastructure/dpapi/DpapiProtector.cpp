#include "infrastructure/dpapi/DpapiProtector.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace tggate::infrastructure::dpapi {
namespace {

#ifdef _WIN32
class OutputBlob final {
public:
    explicit OutputBlob(const bool contains_plain_text) noexcept : contains_plain_text_(contains_plain_text) {}
    ~OutputBlob() noexcept {
        if (blob_.pbData == nullptr) return;
        if (contains_plain_text_ && blob_.cbData != 0) SecureZeroMemory(blob_.pbData, blob_.cbData);
        LocalFree(blob_.pbData);
    }

    OutputBlob(const OutputBlob&) = delete;
    OutputBlob& operator=(const OutputBlob&) = delete;

    [[nodiscard]] DATA_BLOB* get() noexcept { return &blob_; }
    [[nodiscard]] const DATA_BLOB& value() const noexcept { return blob_; }

private:
    DATA_BLOB blob_{};
    bool contains_plain_text_ = false;
};

std::optional<std::vector<unsigned char>> crypt(
    const std::vector<unsigned char>& input,
    const std::string_view purpose,
    const bool protect) {
    DATA_BLOB data{.cbData = static_cast<DWORD>(input.size()), .pbData = const_cast<BYTE*>(input.data())};
    DATA_BLOB entropy{.cbData = static_cast<DWORD>(purpose.size()), .pbData = reinterpret_cast<BYTE*>(const_cast<char*>(purpose.data()))};
    OutputBlob output(!protect);
    const auto success = protect
        ? CryptProtectData(&data, L"TgGate", purpose.empty() ? nullptr : &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, output.get())
        : CryptUnprotectData(&data, nullptr, purpose.empty() ? nullptr : &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, output.get());
    if (!success) return std::nullopt;
    const auto& result_blob = output.value();
    std::vector<unsigned char> result(result_blob.pbData, result_blob.pbData + result_blob.cbData);
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
