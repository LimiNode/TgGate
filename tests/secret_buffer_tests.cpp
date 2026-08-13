#include "application/security/SecretBuffer.hpp"

#include <string_view>
#include <utility>
#include <vector>

int main() {
    std::vector<unsigned char> decrypted_bytes{'s', 'e', 'c', 'r', 'e', 't'};
    tggate::application::security::SecretBuffer secret(std::move(decrypted_bytes));

    return decrypted_bytes.empty() && secret.view() == std::string_view("secret") ? 0 : 1;
}
