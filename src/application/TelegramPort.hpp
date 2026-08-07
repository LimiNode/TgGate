#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tggate::application {

struct Chat {
    std::int64_t id = 0;
    std::string title;
};

struct Message {
    std::int64_t id = 0;
    std::int64_t chat_id = 0;
    std::string text;
};

template <typename Value>
class Result final {
public:
    Result(Value value) : value_(std::move(value)) {}
    Result(std::string error) : error_(std::move(error)) {}

    [[nodiscard]] explicit operator bool() const noexcept { return value_.has_value(); }
    [[nodiscard]] Value& operator*() noexcept { return *value_; }
    [[nodiscard]] const Value& operator*() const noexcept { return *value_; }
    [[nodiscard]] Value* operator->() noexcept { return &*value_; }
    [[nodiscard]] const Value* operator->() const noexcept { return &*value_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    std::optional<Value> value_;
    std::string error_;
};

class ITelegramService {
public:
    virtual ~ITelegramService() = default;

    [[nodiscard]] virtual Result<std::vector<Chat>> list_chats(std::string_view account_id) = 0;
    [[nodiscard]] virtual Result<std::vector<Message>> get_messages(
        std::string_view account_id,
        std::int64_t chat_id,
        std::size_t limit) = 0;
    [[nodiscard]] virtual Result<Message> send_message(
        std::string_view account_id,
        std::int64_t chat_id,
        std::string_view text) = 0;
};

class UnavailableTelegramService final : public ITelegramService {
public:
    [[nodiscard]] Result<std::vector<Chat>> list_chats(std::string_view) override;
    [[nodiscard]] Result<std::vector<Message>> get_messages(std::string_view, std::int64_t, std::size_t) override;
    [[nodiscard]] Result<Message> send_message(std::string_view, std::int64_t, std::string_view) override;
};

} // namespace tggate::application
