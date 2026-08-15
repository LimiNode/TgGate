#include "infrastructure/tdlib/TdTelegramService.hpp"

#include <utility>

namespace tggate::infrastructure::tdlib {

void TdTelegramService::attach(std::string account_id, std::shared_ptr<TdAccount> account) {
    std::scoped_lock lock(mutex_);
    accounts_.insert_or_assign(std::move(account_id), std::move(account));
}

void TdTelegramService::detach(const std::string_view account_id) {
    std::scoped_lock lock(mutex_);
    accounts_.erase(std::string(account_id));
}

std::shared_ptr<TdAccount> TdTelegramService::find(const std::string_view account_id) const {
    std::scoped_lock lock(mutex_);
    const auto found = accounts_.find(std::string(account_id));
    return found == accounts_.end() ? nullptr : found->second;
}

application::Result<std::vector<application::Chat>> TdTelegramService::list_chats(const std::string_view account_id) {
    const auto account = find(account_id);
    if (!account) return std::string("Telegram account is not running");
    return account->list_chats();
}

application::Result<std::vector<application::Message>> TdTelegramService::get_messages(
    const std::string_view account_id, const std::int64_t chat_id, const std::size_t limit) {
    const auto account = find(account_id);
    if (!account) return std::string("Telegram account is not running");
    return account->get_messages(chat_id, limit);
}

application::Result<application::Message> TdTelegramService::send_message(
    std::string_view, std::int64_t, std::string_view) {
    return std::string("Telegram write operations are not implemented yet");
}

} // namespace tggate::infrastructure::tdlib
