#pragma once

#include "infrastructure/tdlib/RequestRouter.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace tggate::infrastructure::tdlib {

enum class DeliveryState { sent, failed, unknown };

struct DeliveryOutcome final {
    DeliveryState state = DeliveryState::unknown;
    std::int64_t message_id = 0;
    std::int64_t chat_id = 0;
    std::string error;
};

[[nodiscard]] constexpr DeliveryState classify_initial_send_failure(
    const bool request_dispatched, const RequestCompletion completion) noexcept {
    return request_dispatched && (completion == RequestCompletion::timed_out || completion == RequestCompletion::closed)
               ? DeliveryState::unknown
               : DeliveryState::failed;
}

// Correlates TDLib's temporary outgoing-message id with its later terminal
// update. It retains only bounded, content-free outcome metadata so an update
// that races ahead of the synchronous sendMessage response is not lost.
class SendDeliveryTracker final {
public:
    void reopen();
    void close();
    void publish_sent(std::int64_t old_message_id, std::int64_t message_id, std::int64_t chat_id);
    void publish_failed(std::int64_t old_message_id, std::string error);
    [[nodiscard]] DeliveryOutcome wait(
        std::int64_t old_message_id, std::chrono::steady_clock::time_point deadline);

private:
    void publish(std::int64_t old_message_id, DeliveryOutcome outcome);

    static constexpr std::size_t kMaximumPendingOutcomes = 64;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::unordered_map<std::int64_t, DeliveryOutcome> outcomes_;
    std::deque<std::int64_t> order_;
    bool closed_ = false;
};

} // namespace tggate::infrastructure::tdlib
