#include "infrastructure/tdlib/TdAccount.hpp"

#include <utility>

int main() {
    tggate::infrastructure::tdlib::TdAccount account;
    tggate::application::security::SecretBuffer code("12345");
    if (account.submit_code(std::move(code)) || !code.empty()) return 1;
    return account.authorization_status() == "Stopped" &&
                   account.last_error() == "TDLib is not waiting for an authentication code"
               ? 0
               : 1;
}
