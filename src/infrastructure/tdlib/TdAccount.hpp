#pragma once

#include "application/security/SecretBuffer.hpp"
#include "application/TelegramPort.hpp"
#include "infrastructure/tdlib/SendDeliveryTracker.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace td {
class ClientManager;
}

namespace tggate::infrastructure::tdlib {

struct TdAccountOptions final {
    std::string account_id;
    std::filesystem::path database_directory;
    std::filesystem::path files_directory;
    std::int32_t api_id = 0;
    application::security::SecretBuffer api_hash;
    application::security::SecretBuffer database_encryption_key;
    std::string phone_number;
};

// The only owner of a TDLib ClientManager for an account. The receive loop is
// serialized here; application/UI code can only observe state and submit the
// short-lived credentials needed by the authorization protocol.
class TdAccount final {
public:
    TdAccount();
    ~TdAccount();
    TdAccount(const TdAccount&) = delete;
    TdAccount& operator=(const TdAccount&) = delete;

    [[nodiscard]] bool begin_authorization(TdAccountOptions options);
    [[nodiscard]] bool submit_code(application::security::SecretBuffer code);
    [[nodiscard]] bool submit_password(application::security::SecretBuffer password);
    [[nodiscard]] application::Result<std::vector<application::Chat>> list_chats();
    [[nodiscard]] application::Result<std::vector<application::Message>> get_messages(
        std::int64_t chat_id, std::size_t limit);
    [[nodiscard]] application::Result<application::SendMessageResult> send_message(
        std::int64_t chat_id, std::string_view text);
    void stop();

    [[nodiscard]] std::string authorization_status() const;
    [[nodiscard]] std::string last_error() const;

private:
    class ReadRequestRouter;
    class ReadResult;

    void receive_loop();
    void process_response();
    void send_tdlib_parameters();
    void send_phone_number();
    void clear_sensitive_options();
    void clear_authorization_input(std::uint64_t request_id);
    [[nodiscard]] bool request_tdlib(
        void* request, ReadResult& result, std::chrono::steady_clock::time_point deadline);
    [[nodiscard]] bool has_read_request(std::uint64_t request_id) const;
    void fulfill_read_request(std::uint64_t request_id, void* response);
    void fail_read_requests(std::string error);
    void process_send_update(void* update);
    void set_status(std::string value);
    void set_error(std::string value);

    std::atomic_bool running_ = false;
    std::thread receive_thread_;
    std::unique_ptr<td::ClientManager> manager_;
    std::int32_t client_id_ = 0;
    std::uint64_t next_request_id_ = 1;
    std::uint64_t authorization_input_request_id_ = 0;
    std::unique_ptr<ReadRequestRouter> read_requests_;
    SendDeliveryTracker delivery_updates_;
    TdAccountOptions options_;
    mutable std::mutex mutex_;
    std::string authorization_status_ = "Stopped";
    std::string last_error_;
    bool stopping_ = false;
};

} // namespace tggate::infrastructure::tdlib
