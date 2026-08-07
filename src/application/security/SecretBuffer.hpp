#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace tggate::application::security {

// This protects only buffers owned by TgGate. A secret copied into a third-party
// request object (for example TDLib) is outside this guarantee.
class SecretBuffer final {
public:
    static constexpr std::size_t max_size = 4096;

    SecretBuffer() = default;
    explicit SecretBuffer(const std::string_view value) { assign(value.data(), value.size()); }
    explicit SecretBuffer(const std::vector<unsigned char>& value) { assign(value.data(), value.size()); }
    explicit SecretBuffer(std::vector<unsigned char>&& value) { assign(value.data(), value.size()); wipe_external(value); }
    ~SecretBuffer() noexcept { clear(); }

    SecretBuffer(const SecretBuffer&) = delete;
    SecretBuffer& operator=(const SecretBuffer&) = delete;

    SecretBuffer(SecretBuffer&& other) noexcept : storage_(other.storage_), size_(other.size_) { other.clear(); }
    SecretBuffer& operator=(SecretBuffer&& other) noexcept {
        if (this != &other) {
            clear();
            storage_ = other.storage_;
            size_ = other.size_;
            other.clear();
        }
        return *this;
    }

    [[nodiscard]] const unsigned char* data() const noexcept { return storage_.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::string_view view() const noexcept {
        return {reinterpret_cast<const char*>(storage_.data()), size_};
    }

    void clear() noexcept {
#ifdef _WIN32
        SecureZeroMemory(storage_.data(), storage_.size());
#else
        volatile unsigned char* current = storage_.data();
        for (std::size_t index = 0; index < storage_.size(); ++index) current[index] = 0;
#endif
        size_ = 0;
    }

private:
    void assign(const void* source, const std::size_t size) {
        if (size > max_size) throw std::invalid_argument("SecretBuffer exceeds the fixed capacity");
        if (size != 0) {
            const auto* bytes = static_cast<const unsigned char*>(source);
            for (std::size_t index = 0; index < size; ++index) storage_[index] = bytes[index];
        }
        size_ = size;
    }

    static void wipe_external(std::vector<unsigned char>& value) noexcept {
        if (value.empty()) return;
#ifdef _WIN32
        SecureZeroMemory(value.data(), value.size());
#else
        volatile unsigned char* current = value.data();
        for (std::size_t index = 0; index < value.size(); ++index) current[index] = 0;
#endif
        value.clear();
    }

    std::array<unsigned char, max_size> storage_{};
    std::size_t size_ = 0;
};

} // namespace tggate::application::security
