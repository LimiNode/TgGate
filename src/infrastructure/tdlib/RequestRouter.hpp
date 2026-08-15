#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tggate::infrastructure::tdlib {

// Correlates asynchronous TDLib responses with synchronous adapter calls. It
// is independent of TDLib so lifecycle and timeout behaviour can be tested
// deterministically without a Telegram account or network access.
template <typename Response>
class RequestRouter final {
public:
    struct RequestState final {
        std::mutex mutex;
        std::condition_variable completed;
        std::optional<Response> response;
        std::string error;
        bool ready = false;
    };

    using Ticket = std::shared_ptr<RequestState>;

    struct Result final {
        std::optional<Response> response;
        std::string error;
    };

    [[nodiscard]] Ticket open(const std::uint64_t request_id) {
        std::scoped_lock lock(mutex_);
        if (!accepting_) return {};
        const auto state = std::make_shared<RequestState>();
        return requests_.emplace(request_id, state).second ? state : Ticket{};
    }

    [[nodiscard]] bool contains(const std::uint64_t request_id) const {
        std::scoped_lock lock(mutex_);
        return requests_.contains(request_id);
    }

    [[nodiscard]] bool discard_late(const std::uint64_t request_id) {
        std::scoped_lock lock(mutex_);
        return retired_.erase(request_id) != 0;
    }

    [[nodiscard]] bool fulfill(const std::uint64_t request_id, Response response) {
        std::shared_ptr<RequestState> state;
        {
            std::scoped_lock lock(mutex_);
            const auto found = requests_.find(request_id);
            if (found == requests_.end()) return false;
            state = std::move(found->second);
            requests_.erase(found);
        }
        {
            std::scoped_lock lock(state->mutex);
            state->response = std::move(response);
            state->ready = true;
        }
        state->completed.notify_one();
        return true;
    }

    [[nodiscard]] Result wait(
        const std::uint64_t request_id,
        const Ticket& state,
        const std::chrono::steady_clock::time_point deadline) {
        if (!state) return {.error = "TDLib request is no longer pending"};
        std::unique_lock state_lock(state->mutex);
        state->completed.wait_until(state_lock, deadline, [&state] { return state->ready; });
        if (!state->ready) {
            state_lock.unlock();
            std::scoped_lock lock(mutex_);
            const auto found = requests_.find(request_id);
            if (found != requests_.end() && found->second == state) {
                requests_.erase(found);
                retire(request_id);
            }
            return {.error = "TDLib read request timed out"};
        }
        return {.response = std::move(state->response), .error = std::move(state->error)};
    }

    void close(std::string error) {
        std::vector<std::shared_ptr<RequestState>> pending;
        {
            std::scoped_lock lock(mutex_);
            accepting_ = false;
            pending.reserve(requests_.size());
            for (auto& [ignored, state] : requests_) {
                retire(ignored);
                pending.push_back(std::move(state));
            }
            requests_.clear();
        }
        for (const auto& state : pending) {
            {
                std::scoped_lock lock(state->mutex);
                state->error = error;
                state->ready = true;
            }
            state->completed.notify_one();
        }
    }

    void reopen() {
        std::scoped_lock lock(mutex_);
        accepting_ = true;
        retired_.clear();
        retired_order_.clear();
    }

private:
    void retire(const std::uint64_t request_id) {
        constexpr std::size_t maximum_retired_requests = 256;
        if (retired_.insert(request_id).second) retired_order_.push_back(request_id);
        while (retired_order_.size() > maximum_retired_requests) {
            retired_.erase(retired_order_.front());
            retired_order_.pop_front();
        }
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<RequestState>> requests_;
    std::unordered_set<std::uint64_t> retired_;
    std::deque<std::uint64_t> retired_order_;
    bool accepting_ = true;
};

} // namespace tggate::infrastructure::tdlib
