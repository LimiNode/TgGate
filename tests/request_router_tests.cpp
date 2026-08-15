#include "infrastructure/tdlib/RequestRouter.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>

using namespace std::chrono_literals;

int main() {
    using Router = tggate::infrastructure::tdlib::RequestRouter<int>;

    Router router;
    const auto first_ticket = router.open(1);
    const auto second_ticket = router.open(2);
    assert(first_ticket && second_ticket);
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
    assert(router.fulfill(2, 22));
    assert(router.fulfill(1, 11));
    first_waiter.join();
    second_waiter.join();
    assert(first == 11 && second == 22);

    const auto timeout_ticket = router.open(3);
    assert(timeout_ticket);
    const auto timed_out = router.wait(3, timeout_ticket, std::chrono::steady_clock::now() + 1ms);
    assert(!timed_out.response && timed_out.error == "TDLib read request timed out");
    assert(!router.contains(3));
    assert(!router.fulfill(3, 33));
    assert(router.discard_late(3));
    assert(!router.discard_late(3));

    const auto stopping_ticket = router.open(4);
    assert(stopping_ticket);
    std::atomic_bool stopped = false;
    std::thread stopping_waiter([&] {
        const auto result = router.wait(4, stopping_ticket, std::chrono::steady_clock::now() + 1s);
        stopped = !result.response && result.error == "TDLib account is stopping";
    });
    router.close("TDLib account is stopping");
    stopping_waiter.join();
    assert(stopped);
    assert(!router.open(5));
    router.reopen();
    const auto reopened_ticket = router.open(5);
    assert(reopened_ticket);
    assert(router.fulfill(5, 55));
    const auto reopened = router.wait(5, reopened_ticket, std::chrono::steady_clock::now() + 1s);
    assert(reopened.response && *reopened.response == 55);
}
