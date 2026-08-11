#include "infrastructure/tdlib/TdAccount.hpp"

#include <type_traits>

int main() {
    using tggate::application::security::SecretBuffer;
    using tggate::infrastructure::tdlib::TdAccount;
    tggate::infrastructure::tdlib::TdAccountOptions options;
    static_assert(std::is_same_v<
        decltype(static_cast<bool (TdAccount::*)(SecretBuffer)>(&TdAccount::submit_code)),
        bool (TdAccount::*)(SecretBuffer)>);
    static_assert(std::is_same_v<
        decltype(static_cast<bool (TdAccount::*)(SecretBuffer)>(&TdAccount::submit_password)),
        bool (TdAccount::*)(SecretBuffer)>);
    SecretBuffer code("12345");
    SecretBuffer moved_code(std::move(code));
    return options.api_hash.empty() && options.database_encryption_key.empty() && code.empty() && !moved_code.empty() ? 0 : 1;
}
