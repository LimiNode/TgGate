#include "infrastructure/tdlib/TdAccount.hpp"
#include "infrastructure/tdlib/TdTelegramService.hpp"

#include <memory>
#include <utility>

int main() {
    auto account = std::make_shared<tggate::infrastructure::tdlib::TdAccount>();
    tggate::application::security::SecretBuffer code("12345");
    if (account->submit_code(std::move(code)) || !code.empty()) return 1;
    if (account->authorization_status() != "Stopped" ||
        account->last_error() != "TDLib is not waiting for an authentication code") return 1;

    tggate::infrastructure::tdlib::TdTelegramService telegram;
    telegram.attach("work", account);
    const auto missing = telegram.list_chats("missing");
    const auto unauthorized = telegram.list_chats("work");
    const auto unauthorized_send = telegram.send_message("work", 42, "hello");
    return !missing && missing.error() == "Telegram account is not running" &&
                   !unauthorized && unauthorized.error() == "Telegram account is not authorized" &&
                   !unauthorized_send && unauthorized_send.error() == "Telegram account is not authorized"
               ? 0
               : 1;
}
