#include "infrastructure/tdlib/TdAccount.hpp"

#include <td/telegram/Client.h>

#include <utility>

namespace tggate::infrastructure::tdlib {
namespace {

using td::td_api::make_object;

std::string state_name(const td::td_api::AuthorizationState& state) {
    using namespace td::td_api;
    switch (state.get_id()) {
    case authorizationStateWaitTdlibParameters::ID: return "Preparing TDLib";
    case authorizationStateWaitPhoneNumber::ID: return "Waiting for phone number";
    case authorizationStateWaitCode::ID: return "Waiting for authentication code";
    case authorizationStateWaitPassword::ID: return "Waiting for 2FA password";
    case authorizationStateWaitRegistration::ID: return "Registration is required";
    case authorizationStateWaitOtherDeviceConfirmation::ID: return "Waiting for confirmation on another device";
    case authorizationStateReady::ID: return "Authorized";
    case authorizationStateLoggingOut::ID: return "Logging out";
    case authorizationStateClosing::ID: return "Closing";
    case authorizationStateClosed::ID: return "Closed";
    default: return "Waiting for a Telegram authorization step";
    }
}

void wipe_string(std::string& value) noexcept {
    volatile char* current = value.empty() ? nullptr : value.data();
    for (std::size_t index = 0; current && index < value.size(); ++index) current[index] = '\0';
    value.clear();
}

class WipeStringOnExit final {
public:
    explicit WipeStringOnExit(std::string& value) noexcept : value_(value) {}
    ~WipeStringOnExit() { wipe_string(value_); }
    WipeStringOnExit(const WipeStringOnExit&) = delete;
    WipeStringOnExit& operator=(const WipeStringOnExit&) = delete;

private:
    std::string& value_;
};

} // namespace

TdAccount::TdAccount() = default;

TdAccount::~TdAccount() {
    stop();
}

bool TdAccount::begin_authorization(TdAccountOptions options) {
    if (options.account_id.empty() || options.api_id <= 0 || options.api_hash.empty() || options.database_encryption_key.empty() || options.phone_number.empty()) {
        set_error("Account id, api_id, api_hash, database key, and phone number are required");
        return false;
    }
    stop();
    {
        std::scoped_lock lock(mutex_);
        options_ = std::move(options);
        last_error_.clear();
        authorization_status_ = "Starting TDLib";
        manager_ = std::make_unique<td::ClientManager>();
        client_id_ = manager_->create_client_id();
        next_request_id_ = 1;
    }
    running_ = true;
    receive_thread_ = std::thread([this] { receive_loop(); });
    process_response(); // getAuthorizationState activates the freshly-created TDLib client.
    return true;
}

bool TdAccount::submit_code(std::string code) {
    if (code.empty()) {
        set_error("Authentication code is required");
        return false;
    }
    std::scoped_lock lock(mutex_);
    if (!manager_ || authorization_status_ != "Waiting for authentication code") {
        last_error_ = "TDLib is not waiting for an authentication code";
        return false;
    }
    manager_->send(client_id_, next_request_id_++, make_object<td::td_api::checkAuthenticationCode>(std::move(code)));
    return true;
}

bool TdAccount::submit_password(std::string password) {
    if (password.empty()) {
        set_error("2FA password is required");
        return false;
    }
    std::scoped_lock lock(mutex_);
    if (!manager_ || authorization_status_ != "Waiting for 2FA password") {
        last_error_ = "TDLib is not waiting for a 2FA password";
        return false;
    }
    manager_->send(client_id_, next_request_id_++, make_object<td::td_api::checkAuthenticationPassword>(std::move(password)));
    return true;
}

void TdAccount::stop() {
    running_ = false;
    if (receive_thread_.joinable()) receive_thread_.join();
    {
        std::scoped_lock lock(mutex_);
        manager_.reset();
        client_id_ = 0;
        authorization_status_ = "Stopped";
    }
    clear_sensitive_options();
    {
        std::scoped_lock lock(mutex_);
        options_ = {};
    }
}

std::string TdAccount::authorization_status() const {
    std::scoped_lock lock(mutex_);
    return authorization_status_;
}

std::string TdAccount::last_error() const {
    std::scoped_lock lock(mutex_);
    return last_error_;
}

void TdAccount::receive_loop() {
    while (running_) {
        td::ClientManager::Response response;
        {
            std::scoped_lock lock(mutex_);
            if (!manager_) return;
            response = manager_->receive(0.1);
        }
        if (!response.object) continue;
        if (response.object->get_id() == td::td_api::error::ID) {
            const auto& error = static_cast<const td::td_api::error&>(*response.object);
            set_error("TDLib " + std::to_string(error.code_) + ": " + error.message_);
            continue;
        }
        if (response.object->get_id() != td::td_api::updateAuthorizationState::ID) continue;
        const auto& update = static_cast<const td::td_api::updateAuthorizationState&>(*response.object);
        if (!update.authorization_state_) continue;
        set_status(state_name(*update.authorization_state_));
        switch (update.authorization_state_->get_id()) {
        case td::td_api::authorizationStateWaitTdlibParameters::ID: send_tdlib_parameters(); break;
        case td::td_api::authorizationStateWaitPhoneNumber::ID: send_phone_number(); break;
        case td::td_api::authorizationStateClosed::ID:
            // This runs on the receive thread, so stop() would attempt to
            // join the current thread. Clear the long-lived sensitive state
            // directly and leave manager ownership for the later stop().
            clear_sensitive_options();
            running_ = false;
            break;
        default: break;
        }
    }
}

void TdAccount::process_response() {
    std::scoped_lock lock(mutex_);
    if (manager_) manager_->send(client_id_, next_request_id_++, make_object<td::td_api::getAuthorizationState>());
}

void TdAccount::send_tdlib_parameters() {
    std::scoped_lock lock(mutex_);
    if (!manager_) return;
    std::string database_key(options_.database_encryption_key.view());
    std::string api_hash(options_.api_hash.view());
    const WipeStringOnExit wipe_database_key(database_key);
    const WipeStringOnExit wipe_api_hash(api_hash);
    auto parameters = make_object<td::td_api::setTdlibParameters>(
        false, options_.database_directory.string(), options_.files_directory.string(), database_key,
        true, true, true, false, options_.api_id, api_hash, "en", "TgGate", "Windows", "0.1.0");
    // TDLib owns its request copy after send(). Wipe the temporary strings as
    // soon as that hand-off is complete; TgGate keeps its canonical copies in
    // SecretBuffer for the account lifecycle.
    manager_->send(client_id_, next_request_id_++, std::move(parameters));
}

void TdAccount::send_phone_number() {
    std::scoped_lock lock(mutex_);
    if (!manager_) return;
    manager_->send(client_id_, next_request_id_++,
        make_object<td::td_api::setAuthenticationPhoneNumber>(options_.phone_number, nullptr));
}

void TdAccount::clear_sensitive_options() {
    std::scoped_lock lock(mutex_);
    options_.api_hash.clear();
    options_.database_encryption_key.clear();
    wipe_string(options_.phone_number);
}

void TdAccount::set_status(std::string value) {
    std::scoped_lock lock(mutex_);
    authorization_status_ = std::move(value);
}

void TdAccount::set_error(std::string value) {
    std::scoped_lock lock(mutex_);
    last_error_ = std::move(value);
}

} // namespace tggate::infrastructure::tdlib
