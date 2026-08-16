#include "infrastructure/tdlib/SendDeliveryTracker.hpp"

#include <cassert>
#include <chrono>
#include <iostream>

int main() {
    using namespace std::chrono_literals;
    using tggate::infrastructure::tdlib::DeliveryState;
    using tggate::infrastructure::tdlib::SendDeliveryTracker;

    SendDeliveryTracker tracker;
    tracker.reopen();
    tracker.publish_sent(-10, 100, 42);
    const auto early_success = tracker.wait(-10, std::chrono::steady_clock::now());
    assert(early_success.state == DeliveryState::sent && early_success.message_id == 100 && early_success.chat_id == 42);

    tracker.publish_failed(-11, "TDLib 500: send failed");
    const auto failure = tracker.wait(-11, std::chrono::steady_clock::now());
    assert(failure.state == DeliveryState::failed && failure.error == "TDLib 500: send failed");

    const auto timeout = tracker.wait(-12, std::chrono::steady_clock::now());
    assert(timeout.state == DeliveryState::unknown);

    tracker.close("TDLib account stopped");
    const auto closed = tracker.wait(-13, std::chrono::steady_clock::now() + 1s);
    assert(closed.state == DeliveryState::failed && closed.error == "TDLib account stopped");
    std::cout << "TgGate TDLib send delivery tracker tests passed\n";
}
