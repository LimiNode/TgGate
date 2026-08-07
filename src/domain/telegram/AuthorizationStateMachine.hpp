#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace tggate::domain::telegram {

enum class AuthorizationState {
    stopped,
    wait_tdlib_parameters,
    wait_phone_number,
    wait_email_address,
    wait_email_code,
    wait_code,
    wait_registration,
    wait_password,
    wait_other_device_confirmation,
    ready,
    logging_out,
    closing,
    closed,
    failed,
};

enum class AuthorizationInput { phone_number, email_address, email_code, code, registration, password };

struct AuthorizationSnapshot final {
    std::string account_id;
    std::uint64_t generation = 0;
    AuthorizationState state = AuthorizationState::stopped;
    bool input_in_flight = false;
    std::string detail;
};

// Owns no TDLib types. It ensures that UI commands are accepted only for the
// current account/session/state and prevents duplicate submits until TDLib emits
// the next update or an error.
class AuthorizationStateMachine final {
public:
    [[nodiscard]] std::uint64_t begin(std::string account_id);
    void observe(std::uint64_t generation, AuthorizationState state, std::string detail = {});
    void fail(std::uint64_t generation, std::string detail);
    [[nodiscard]] bool begin_input(std::uint64_t generation, AuthorizationInput input, std::string& error);
    [[nodiscard]] const AuthorizationSnapshot& snapshot() const noexcept;

private:
    [[nodiscard]] bool accepts(AuthorizationInput input) const noexcept;
    AuthorizationSnapshot snapshot_;
};

} // namespace tggate::domain::telegram
