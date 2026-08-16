#pragma once

#include "application/TelegramPort.hpp"
#include "infrastructure/tdlib/TdAccount.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace tggate::infrastructure::tdlib {

// Owns no TDLib clients itself. It is the application-facing account registry
// and keeps TDLib confined to the infrastructure layer.
class TdTelegramService final : public application::ITelegramService {
public:
    void attach(std::string account_id, std::shared_ptr<TdAccount> account);
    void detach(std::string_view account_id);
    [[nodiscard]] std::shared_ptr<TdAccount> find(std::string_view account_id) const;

    [[nodiscard]] application::Result<std::vector<application::Chat>> list_chats(
        std::string_view account_id) override;
    [[nodiscard]] application::Result<std::vector<application::Message>> get_messages(
        std::string_view account_id, std::int64_t chat_id, std::size_t limit) override;
    [[nodiscard]] application::Result<application::SendMessageResult> send_message(
        std::string_view account_id, std::int64_t chat_id, std::string_view text) override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<TdAccount>> accounts_;
};

} // namespace tggate::infrastructure::tdlib
