#include "infrastructure/tdlib/TdAccount.hpp"
#include "infrastructure/tdlib/RequestRouter.hpp"

#include <td/telegram/Client.h>

#include <chrono>
#include <condition_variable>
#include <utility>
#include <vector>

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
    ~WipeStringOnExit() noexcept { wipe_string(value_); }
    WipeStringOnExit(const WipeStringOnExit&) = delete;
    WipeStringOnExit& operator=(const WipeStringOnExit&) = delete;

private:
    std::string& value_;
};

constexpr auto kReadRequestTimeout = std::chrono::seconds(10);

} // namespace

class TdAccount::ReadRequestRouter final {
public:
    RequestRouter<td::td_api::object_ptr<td::td_api::Object>> router;
};

class TdAccount::ReadResult final {
public:
    td::td_api::object_ptr<td::td_api::Object> response;
    std::string error;
};

TdAccount::TdAccount() : read_requests_(std::make_unique<ReadRequestRouter>()) {}

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
        read_requests_->router.reopen();
        options_ = std::move(options);
        last_error_.clear();
        authorization_status_ = "Starting TDLib";
        stopping_ = false;
        manager_ = std::make_unique<td::ClientManager>();
        client_id_ = manager_->create_client_id();
        next_request_id_ = 1;
        running_ = true;
    }
    receive_thread_ = std::thread([this] { receive_loop(); });
    process_response(); // getAuthorizationState activates the freshly-created TDLib client.
    return true;
}

bool TdAccount::submit_code(application::security::SecretBuffer code) {
    if (code.empty()) {
        set_error("Authentication code is required");
        return false;
    }
    std::scoped_lock lock(mutex_);
    if (authorization_input_request_id_ != 0) {
        last_error_ = "An authorization input request is already in flight";
        return false;
    }
    if (!manager_ || authorization_status_ != "Waiting for authentication code") {
        last_error_ = "TDLib is not waiting for an authentication code";
        return false;
    }
    std::string plain_code(code.view());
    const WipeStringOnExit wipe_code(plain_code);
    const auto request_id = next_request_id_++;
    manager_->send(client_id_, request_id, make_object<td::td_api::checkAuthenticationCode>(std::move(plain_code)));
    authorization_input_request_id_ = request_id;
    return true;
}

bool TdAccount::submit_password(application::security::SecretBuffer password) {
    if (password.empty()) {
        set_error("2FA password is required");
        return false;
    }
    std::scoped_lock lock(mutex_);
    if (authorization_input_request_id_ != 0) {
        last_error_ = "An authorization input request is already in flight";
        return false;
    }
    if (!manager_ || authorization_status_ != "Waiting for 2FA password") {
        last_error_ = "TDLib is not waiting for a 2FA password";
        return false;
    }
    std::string plain_password(password.view());
    const WipeStringOnExit wipe_password(plain_password);
    const auto request_id = next_request_id_++;
    manager_->send(client_id_, request_id, make_object<td::td_api::checkAuthenticationPassword>(std::move(plain_password)));
    authorization_input_request_id_ = request_id;
    return true;
}

application::Result<std::vector<application::Chat>> TdAccount::list_chats() {
    const auto deadline = std::chrono::steady_clock::now() + kReadRequestTimeout;
    ReadResult listed;
    auto request = make_object<td::td_api::getChats>(make_object<td::td_api::chatListMain>(), 100);
    if (!request_tdlib(request.release(), listed, deadline)) return std::move(listed.error);
    if (!listed.response || listed.response->get_id() != td::td_api::chats::ID) {
        return std::string("TDLib returned an unexpected chat list response");
    }

    const auto& listed_chats = static_cast<const td::td_api::chats&>(*listed.response);
    std::vector<application::Chat> result;
    result.reserve(listed_chats.chat_ids_.size());
    for (const auto chat_id : listed_chats.chat_ids_) {
        ReadResult chat_result;
        auto chat_request = make_object<td::td_api::getChat>(chat_id);
        if (!request_tdlib(chat_request.release(), chat_result, deadline)) return std::move(chat_result.error);
        if (!chat_result.response || chat_result.response->get_id() != td::td_api::chat::ID) {
            return std::string("TDLib returned an unexpected chat response");
        }
        const auto& chat = static_cast<const td::td_api::chat&>(*chat_result.response);
        result.push_back({.id = chat.id_, .title = chat.title_});
    }
    return result;
}

application::Result<std::vector<application::Message>> TdAccount::get_messages(
    const std::int64_t chat_id, const std::size_t limit) {
    if (chat_id == 0 || limit == 0 || limit > 100) return std::string("Chat id and a message limit from 1 to 100 are required");
    ReadResult history;
    auto request = make_object<td::td_api::getChatHistory>(chat_id, 0, 0, static_cast<std::int32_t>(limit), false);
    if (!request_tdlib(request.release(), history, std::chrono::steady_clock::now() + kReadRequestTimeout)) {
        return std::move(history.error);
    }
    if (!history.response || history.response->get_id() != td::td_api::messages::ID) {
        return std::string("TDLib returned an unexpected message history response");
    }

    const auto& history_messages = static_cast<const td::td_api::messages&>(*history.response);
    std::vector<application::Message> result;
    result.reserve(history_messages.messages_.size());
    for (const auto& message : history_messages.messages_) {
        if (!message || !message->content_ || message->content_->get_id() != td::td_api::messageText::ID) continue;
        const auto& content = static_cast<const td::td_api::messageText&>(*message->content_);
        if (!content.text_) continue;
        result.push_back({.id = message->id_, .chat_id = message->chat_id_, .text = content.text_->text_});
    }
    return result;
}

application::Result<application::Message> TdAccount::send_message(
    const std::int64_t chat_id, const std::string_view text) {
    if (chat_id == 0 || text.empty() || text.size() > 4096) {
        return std::string("Chat id and message text from 1 to 4096 bytes are required");
    }
    std::string plain_text(text);
    const WipeStringOnExit wipe_text(plain_text);
    auto content = make_object<td::td_api::inputMessageText>(
        make_object<td::td_api::formattedText>(plain_text, td::td_api::array<td::td_api::object_ptr<td::td_api::textEntity>>{}),
        nullptr, false);
    auto request = make_object<td::td_api::sendMessage>(chat_id, nullptr, nullptr, nullptr, nullptr, std::move(content));
    ReadResult response;
    if (!request_tdlib(request.release(), response, std::chrono::steady_clock::now() + kReadRequestTimeout)) {
        return std::move(response.error);
    }
    if (!response.response || response.response->get_id() != td::td_api::message::ID) {
        return std::string("TDLib returned an unexpected send-message response");
    }
    const auto& message = static_cast<const td::td_api::message&>(*response.response);
    return application::Message{.id = message.id_, .chat_id = message.chat_id_};
}

void TdAccount::stop() {
    {
        std::scoped_lock lock(mutex_);
        if (manager_ && !stopping_) {
            stopping_ = true;
            authorization_status_ = "Closing";
            manager_->send(client_id_, next_request_id_++, make_object<td::td_api::close>());
        }
    }
    fail_read_requests("TDLib account stopped");
    if (receive_thread_.joinable()) receive_thread_.join();
    {
        std::scoped_lock lock(mutex_);
        manager_.reset();
        client_id_ = 0;
        authorization_input_request_id_ = 0;
        authorization_status_ = "Stopped";
        stopping_ = false;
        running_ = false;
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
        if (has_read_request(response.request_id)) {
            fulfill_read_request(response.request_id, response.object.release());
            continue;
        }
        if (read_requests_->router.discard_late(response.request_id)) continue;
        if (response.object->get_id() == td::td_api::error::ID) {
            clear_authorization_input(response.request_id);
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
            fail_read_requests("TDLib authorization was closed");
            {
                std::scoped_lock lock(mutex_);
                stopping_ = true;
                running_ = false;
            }
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

void TdAccount::clear_authorization_input(const std::uint64_t request_id) {
    std::scoped_lock lock(mutex_);
    if (authorization_input_request_id_ == request_id) authorization_input_request_id_ = 0;
}

bool TdAccount::request_tdlib(
    void* raw_request, ReadResult& result, const std::chrono::steady_clock::time_point deadline) {
    td::td_api::object_ptr<td::td_api::Function> request(static_cast<td::td_api::Function*>(raw_request));
    std::uint64_t request_id = 0;
    RequestRouter<td::td_api::object_ptr<td::td_api::Object>>::Ticket ticket;
    {
        std::scoped_lock lock(mutex_);
        if (!running_ || stopping_ || !manager_ || authorization_status_ != "Authorized") {
            result.error = "Telegram account is not authorized";
            return false;
        }
        request_id = next_request_id_++;
        ticket = read_requests_->router.open(request_id);
        if (!ticket) {
            result.error = "Telegram account is stopping";
            return false;
        }
        manager_->send(client_id_, request_id, std::move(request));
    }
    auto response = read_requests_->router.wait(request_id, ticket, deadline);
    result.error = std::move(response.error);
    if (!result.error.empty()) return false;
    if (!response.response) {
        result.error = "TDLib read request completed without a response";
        return false;
    }
    result.response = std::move(*response.response);
    if (result.response && result.response->get_id() == td::td_api::error::ID) {
        const auto& error = static_cast<const td::td_api::error&>(*result.response);
        result.error = "TDLib " + std::to_string(error.code_) + ": " + error.message_;
        return false;
    }
    return result.response != nullptr;
}

bool TdAccount::has_read_request(const std::uint64_t request_id) const {
    return read_requests_->router.contains(request_id);
}

void TdAccount::fulfill_read_request(const std::uint64_t request_id, void* raw_response) {
    td::td_api::object_ptr<td::td_api::Object> response(static_cast<td::td_api::Object*>(raw_response));
    static_cast<void>(read_requests_->router.fulfill(request_id, std::move(response)));
}

void TdAccount::fail_read_requests(std::string error) {
    read_requests_->router.close(std::move(error));
}

void TdAccount::set_status(std::string value) {
    std::scoped_lock lock(mutex_);
    authorization_status_ = std::move(value);
    authorization_input_request_id_ = 0;
}

void TdAccount::set_error(std::string value) {
    std::scoped_lock lock(mutex_);
    last_error_ = std::move(value);
}

} // namespace tggate::infrastructure::tdlib
