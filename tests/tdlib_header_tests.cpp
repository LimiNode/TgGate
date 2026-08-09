#include "infrastructure/tdlib/TdAccount.hpp"

int main() {
    tggate::infrastructure::tdlib::TdAccountOptions options;
    return options.api_hash.empty() && options.database_encryption_key.empty() ? 0 : 1;
}
