#include <jjc/detail/wait.hpp>

#include <cstring>
#include <limits>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

#if !defined(FUTEX_PRIVATE_FLAG)
#define FUTEX_WAIT_PRIVATE FUTEX_WAIT
#define FUTEX_WAKE_PRIVATE FUTEX_WAKE
#endif

namespace jjc::detail::concurrency {

__always_inline
int futex(void* uaddr, int op, uint32_t val, const timespec* timeout, int* uaddr2, int val3) {
    return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

int wait_impl(void* obj, void* expected) noexcept {
    uint32_t e;
    std::memcpy(&e, expected, sizeof(e));
    return futex(obj, FUTEX_WAIT_PRIVATE, e, nullptr, nullptr, 0);
}

int wait_for_impl(void* obj, void* expected, const std::chrono::milliseconds& d) noexcept {    
    uint32_t e;
    std::memcpy(&e, expected, sizeof(e));
    const auto s = std::chrono::duration_cast<std::chrono::seconds>(d);
    const auto n = std::chrono::duration_cast<std::chrono::nanoseconds>(d - s);
    const auto t = timespec { s.count(), n.count() };
    return futex(obj, FUTEX_WAIT_PRIVATE, e, &t, nullptr, 0);
}

int wake_impl(void* obj, uint32_t count) noexcept {
    return futex(obj, FUTEX_WAKE_PRIVATE, count, nullptr, nullptr, 0);
}

int wake_all_impl(void* obj) noexcept {
    return wake_impl(obj, std::numeric_limits<int>::max());
}

}