#ifndef JJC_LATCH_HPP
#define JJC_LATCH_HPP

#include <atomic>
#include <cassert>
#include <cstdint>
#include <jjc/detail/wait.hpp>
#include <limits>

namespace jjc {

class latch {
public:
    static constexpr std::ptrdiff_t max() noexcept {
        return std::numeric_limits<uint32_t>::max();
    }

    constexpr explicit latch(std::ptrdiff_t expected) :
        _value { static_cast<uint32_t>(expected) }
    {
        assert(expected >= 0 && expected <= max());
    }

    latch(const latch&) = delete;

    latch& operator =(const latch&) = delete;

    void count_down(std::ptrdiff_t n = 1) {
        assert(0 <= n && n <= max());
        if (n == _value.fetch_sub(static_cast<uint32_t>(n), std::memory_order_release)) {
            // unlike with semaphore, the impl is simplified here by assuming
            // the latch will always have at least one waiting thread
            detail::concurrency::wake_all(&_value);
        }
    }

    bool try_wait() const noexcept {
        return 0 == _value.load(std::memory_order_acquire);
    }

    void wait() const {
        while (true) {
            auto cur = _value.load(std::memory_order_acquire);
            if (cur == 0) return;
            detail::concurrency::wait(&_value, cur);
        }
    }

    void arrive_and_wait(std::ptrdiff_t n = 1) {
        assert(0 <= n && n <= max());
        auto count = static_cast<uint32_t>(n);
        auto cur = _value.fetch_sub(count, std::memory_order_acq_rel) - count;
        if (cur == 0) {
            detail::concurrency::wake_all(&_value);
            return;
        }
        do {
            detail::concurrency::wait(&_value, cur);
            cur = _value.load(std::memory_order_acquire);

        } while (cur != 0);
    }

private:
    mutable std::atomic_uint32_t _value;
};

}

#endif//JJC_LATCH_HPP