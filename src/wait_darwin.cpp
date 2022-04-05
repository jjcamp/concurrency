#include <jjc/detail/wait.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>

// disclaimer: this is untested, entirely based off of Apple's BSD sources

extern "C" {

// https://opensource.apple.com/source/xnu/xnu-3789.1.32/bsd/sys/ulock.h.auto.html
int __ulock_wait(uint32_t op, void* addr, uint64_t value, uint32_t timeout_us);
int __ulock_wake(uint32_t op, void* addr, uint64_t wake_value);

}

namespace {

// op
constexpr uint32_t compare_and_wait = 1;
constexpr uint32_t wake_one_flag = 0;
constexpr uint32_t wake_all_flag = 1 << 2;

// timeout
constexpr uint32_t infinite = 0;

using microseconds = std::chrono::duration<uint32_t, std::micro>;

}

namespace jjc::detail::concurrency {

int wait_impl(void* obj, void* expected) noexcept {
    uint64_t e;
    std::memcpy(&e, expected, 4);
    return __ulock_wait(compare_and_wait, obj, e, infinite);
}

int wait_for_impl(void* obj, void* expected, const std::chrono::milliseconds& d) noexcept {
    uint64_t e;
    std::memcpy(&e, expected, 4);
    if (d == std::chrono::milliseconds::zero()) return 1;
    const auto us = std::chrono::duration_cast<microseconds>(d);
    return __ulock_wait(compare_and_wait, obj, e, us.count());
}

int wake_impl(void* obj, uint32_t count) noexcept {
    const auto wake_flag = count == 1 ? wake_one_flag : wake_all_flag;
    return __ulock_wake(compare_and_wait | wake_flag, obj, 0);
}

int wake_all_impl(void* obj) noexcept {
    return __ulock_wake(compare_and_wait | wake_all_flag, obj, 0);
}

}