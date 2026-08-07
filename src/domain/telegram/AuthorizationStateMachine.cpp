#include "domain/telegram/AuthorizationStateMachine.hpp"

namespace tggate::domain::telegram {

std::uint64_t AuthorizationStateMachine::begin(std::string account_id) {
    snapshot_.account_id = std::move(account_id);
    ++snapshot_.generation;
    snapshot_.state = AuthorizationState::wait_tdlib_parameters;
    snapshot_.input_in_flight = false;
    snapshot_.detail.clear();
    return snapshot_.generation;
}

void AuthorizationStateMachine::observe(const std::uint64_t generation, const AuthorizationState state, std::string detail) {
    if (generation != snapshot_.generation) return;
    snapshot_.state = state;
    snapshot_.detail = std::move(detail);
    snapshot_.input_in_flight = false;
}

void AuthorizationStateMachine::fail(const std::uint64_t generation, std::string detail) {
    if (generation != snapshot_.generation) return;
    snapshot_.state = AuthorizationState::failed;
    snapshot_.detail = std::move(detail);
    snapshot_.input_in_flight = false;
}

bool AuthorizationStateMachine::begin_input(
    const std::uint64_t generation,
    const AuthorizationInput input,
    std::string& error) {
    if (generation != snapshot_.generation) {
        error = "Authorization input belongs to a stale account session";
        return false;
    }
    if (snapshot_.input_in_flight) {
        error = "An authorization request is already in flight";
        return false;
    }
    if (!accepts(input)) {
        error = "TDLib is not requesting this authorization input";
        return false;
    }
    snapshot_.input_in_flight = true;
    return true;
}

const AuthorizationSnapshot& AuthorizationStateMachine::snapshot() const noexcept { return snapshot_; }

bool AuthorizationStateMachine::accepts(const AuthorizationInput input) const noexcept {
    switch (input) {
    case AuthorizationInput::phone_number: return snapshot_.state == AuthorizationState::wait_phone_number;
    case AuthorizationInput::email_address: return snapshot_.state == AuthorizationState::wait_email_address;
    case AuthorizationInput::email_code: return snapshot_.state == AuthorizationState::wait_email_code;
    case AuthorizationInput::code: return snapshot_.state == AuthorizationState::wait_code;
    case AuthorizationInput::registration: return snapshot_.state == AuthorizationState::wait_registration;
    case AuthorizationInput::password: return snapshot_.state == AuthorizationState::wait_password;
    }
    return false;
}

} // namespace tggate::domain::telegram
