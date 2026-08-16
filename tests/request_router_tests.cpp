#include "infrastructure/tdlib/RequestRouter.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace std::chrono_literals;

int main() {
    using Router = tggate::infrastructure::tdlib::RequestRouter<int>;

    Router router;
    const auto first_ticket = router.open(1);
    const auto second_ticket = router.open(2);
    if (!first_ticket || !second_ticket) return 1;
    std::atomic_int first = 0;
    std::atomic_int second = 0;
    std::thread first_waiter([&] {
        const auto result = router.wait(1, first_ticket, std::chrono::steady_clock::now() + 1s);
        first = result.response.value_or(-1);
    });
    std::thread second_waiter([&] {
        const auto result = router.wait(2, second_ticket, std::chrono::steady_clock::now() + 1s);
        second = result.response.value_or(-1);
    });
    if (!router.fulfill(2, 22) || !router.fulfill(1, 11)) return 1;
    first_waiter.join();
    second_waiter.join();
    if (first != 11 || second != 22) return 1;

    const auto timeout_ticket = router.open(3);
    if (!timeout_ticket) return 1;
    const auto timed_out = router.wait(3, timeout_ticket, std::chrono::steady_clock::now() + 1ms);
    if (timed_out.response || timed_out.error != "TDLib request timed out" ||
        timed_out.completion != tggate::infrastructure::tdlib::RequestCompletion::timed_out || router.contains(3) ||
        router.fulfill(3, 33) || !router.discard_late(3) || router.discard_late(3)) return 1;

    const auto stopping_ticket = router.open(4);
    if (!stopping_ticket) return 1;
    std::atomic_bool stopped = false;
    std::thread stopping_waiter([&] {
        const auto result = router.wait(4, stopping_ticket, std::chrono::steady_clock::now() + 1s);
        stopped = !result.response && result.error == "TDLib account is stopping" &&
            result.completion == tggate::infrastructure::tdlib::RequestCompletion::closed;
    });
    router.close("TDLib account is stopping");
    stopping_waiter.join();
    if (!stopped || router.open(5)) return 1;
    router.reopen();
    const auto reopened_ticket = router.open(5);
    if (!reopened_ticket || !router.fulfill(5, 55)) return 1;
    const auto reopened = router.wait(5, reopened_ticket, std::chrono::steady_clock::now() + 1s);
    return reopened.response && *reopened.response == 55 &&
                   reopened.completion == tggate::infrastructure::tdlib::RequestCompletion::response
               ? 0
               : 1;
}
