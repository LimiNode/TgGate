#include "infrastructure/tdlib/SendDeliveryTracker.hpp"

#include <algorithm>
#include <utility>

namespace tggate::infrastructure::tdlib {

void SendDeliveryTracker::reopen() {
    std::scoped_lock lock(mutex_);
    outcomes_.clear();
    order_.clear();
    closed_ = false;
}

void SendDeliveryTracker::close() {
    {
        std::scoped_lock lock(mutex_);
        closed_ = true;
    }
    changed_.notify_all();
}

void SendDeliveryTracker::publish_sent(
    const std::int64_t old_message_id, const std::int64_t message_id, const std::int64_t chat_id) {
    publish(old_message_id, {.state = DeliveryState::sent, .message_id = message_id, .chat_id = chat_id});
}

void SendDeliveryTracker::publish_failed(const std::int64_t old_message_id, std::string error) {
    publish(old_message_id, {.state = DeliveryState::failed, .error = std::move(error)});
}

DeliveryOutcome SendDeliveryTracker::wait(
    const std::int64_t old_message_id, const std::chrono::steady_clock::time_point deadline) {
    std::unique_lock lock(mutex_);
    changed_.wait_until(lock, deadline, [this, old_message_id] {
        return closed_ || outcomes_.contains(old_message_id);
    });
    if (const auto found = outcomes_.find(old_message_id); found != outcomes_.end()) {
        auto outcome = std::move(found->second);
        outcomes_.erase(found);
        const auto order_entry = std::find(order_.begin(), order_.end(), old_message_id);
        if (order_entry != order_.end()) order_.erase(order_entry);
        return outcome;
    }
    if (closed_) return {.state = DeliveryState::unknown};
    return {.state = DeliveryState::unknown};
}

void SendDeliveryTracker::publish(const std::int64_t old_message_id, DeliveryOutcome outcome) {
    {
        std::scoped_lock lock(mutex_);
        const auto [iterator, inserted] = outcomes_.insert_or_assign(old_message_id, std::move(outcome));
        static_cast<void>(iterator);
        if (inserted) order_.push_back(old_message_id);
        while (order_.size() > kMaximumPendingOutcomes) {
            outcomes_.erase(order_.front());
            order_.pop_front();
        }
    }
    changed_.notify_all();
}

} // namespace tggate::infrastructure::tdlib
