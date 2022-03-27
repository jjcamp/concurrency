#ifndef JJC_SEMAPHORE_HPP
#define JJC_SEMAPHORE_HPP

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <jjc/detail/wait.hpp>
#include <limits>

namespace jjc {

template<std::ptrdiff_t least_max_value = std::numeric_limits<uint32_t>::max()>
class counting_semaphore {
public:
    static_assert(least_max_value >= 0, "least_max_value must be positive");

    static constexpr std::ptrdiff_t max() noexcept {
        return std::numeric_limits<uint32_t>::max();
    }

    static_assert(least_max_value <= max(), "least_max_value is too large");

    constexpr explicit counting_semaphore(std::ptrdiff_t desired) :
        _data({ static_cast<uint32_t>(desired), 0 })
    {
        assert(desired >= 0 && desired <= max());
    }

    counting_semaphore(const counting_semaphore&) = delete;

    ~counting_semaphore() noexcept = default;

    counting_semaphore& operator =(const counting_semaphore&) = delete;

    void release(std::ptrdiff_t update = 1) {
        if (update == 0) return;
        assert(update > 0 && update < max());
        auto count = static_cast<uint32_t>(update);
        auto prev = _data.load(std::memory_order_relaxed);
        while (!_data.compare_exchange_weak(prev, prev.add_value(count), std::memory_order_release, std::memory_order_relaxed)) {}
        assert((prev.value + count) <= least_max_value); // update value caused semaphore to overflow least_max_value
        if (prev.waiting == 0) return;
        detail::concurrency::wake(reinterpret_cast<uint32_t*>(&_data), std::min(count, prev.waiting));
    }

    void acquire() {
        auto cur = _data.load(std::memory_order_relaxed);
        if (
            cur.value != 0 &&
            _data.compare_exchange_strong(cur, cur.add(-1, 0), std::memory_order_acquire, std::memory_order_relaxed)
        ) return;

        while (!_data.compare_exchange_weak(cur, cur.add(0, 1), std::memory_order_relaxed)) {}

        while (true) {
            if (cur.value == 0) {
                detail::concurrency::wait(reinterpret_cast<uint32_t*>(&_data), cur.value);
                cur = _data.load(std::memory_order_relaxed);
            }
            else if (_data.compare_exchange_weak(cur, cur.add(-1, -1), std::memory_order_acquire, std::memory_order_relaxed)) {
                return;
            }
        }
    }

    bool try_acquire() noexcept {
        auto cur = _data.load(std::memory_order_relaxed);
        while (cur.value != 0) {
            if (_data.compare_exchange_strong(cur, cur.add(-1, 0), std::memory_order_acquire, std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    template<typename Rep, typename Period>
    bool try_acquire_for(const std::chrono::duration<Rep, Period>& d) {
        const auto t = std::chrono::steady_clock::now() + d;
        return try_acquire_until(t);
    }

    template<typename Clock, typename Duration>
    bool try_acquire_until(const std::chrono::time_point<Clock, Duration>& t) {
        auto cur = _data.load(std::memory_order_relaxed);
        if (
            cur.value != 0 &&
            _data.compare_exchange_strong(cur, cur.add(-1, 0), std::memory_order_acquire, std::memory_order_relaxed)
        ) return true;

        while (!_data.compare_exchange_weak(cur, cur.add(0, 1), std::memory_order_relaxed)) {}

        while (true) {
            const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t - Clock::now());
            if (dt <= std::chrono::milliseconds::zero()) {
                while (!_data.compare_exchange_weak(cur, cur.add(0, -1), std::memory_order_relaxed)) {}
                return false;
            }
            if (cur.value == 0) {
                detail::concurrency::wait_for(reinterpret_cast<uint32_t*>(&_data), cur.value, dt);
                cur = _data.load(std::memory_order_relaxed);
            }
            else if (_data.compare_exchange_strong(cur, cur.add(-1, -1), std::memory_order_acquire, std::memory_order_relaxed)) {
                return true;
            }
        }
    }

private:
    struct data {
        uint32_t value;
        uint32_t waiting;

        data add(int v, int w) const { return { value + v, waiting + w}; }
        data add_value(uint32_t count) const { return { value + count, waiting }; }
    };

    static_assert(std::atomic<data>::is_always_lock_free);

    std::atomic<data> _data;
};

template<>
class counting_semaphore<1> {
public:
    static constexpr std::ptrdiff_t max() noexcept { return 1; }

    constexpr counting_semaphore(ptrdiff_t desired = 0) :
        _value(desired ? 1 : 0)
    {
        assert((desired & ~1) == 0);
    }

    counting_semaphore(const counting_semaphore&) = delete;

    ~counting_semaphore() noexcept = default;

    counting_semaphore& operator =(const counting_semaphore&) = delete;

    void release(std::ptrdiff_t update = 1) {
        if (update == 0) return;
        assert(update == 1);
        if (-1 == _value.exchange(1, std::memory_order_acq_rel)) {
            detail::concurrency::wake(&_value, 1);
        }
    }

    void acquire() {
        // If the semaphore is in the waiting state, then it must go back to the
        // waiting state upon a successful acquire. This incurs an extra call to
        // wake when there is only one thread waiting, but is required for
        // correctness.
        auto next = 0;
        auto prev = _value.load(std::memory_order_relaxed);
        while (true) {
            if (prev == 1 && _value.compare_exchange_strong(prev, next, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                // Acquired
                return;
            }
            if (prev == -1 || _value.compare_exchange_strong(prev, -1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                next = -1;
                detail::concurrency::wait(&_value, prev);
                prev = _value.load(std::memory_order_relaxed);
            }
        }
    }

    bool try_acquire() {
        auto prev = 1;
        return _value.compare_exchange_strong(prev, 0, std::memory_order_acq_rel, std::memory_order_relaxed);
    }

    template<typename Rep, typename Period>
    bool try_acquire_for(const std::chrono::duration<Rep, Period>& d) {
        const auto t = std::chrono::steady_clock::now() + d;
        return try_acquire_until(t);
    }

    template<typename Clock, typename Duration>
    bool try_acquire_until(const std::chrono::time_point<Clock, Duration>& t) {
        auto next = 0;
        auto prev = _value.load(std::memory_order_relaxed);
        while (true) {
            if (prev == 1 && _value.compare_exchange_strong(prev, next, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return true;
            }
            if (prev == -1 || _value.compare_exchange_strong(prev, -1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t - Clock::now());
                if (dt <= std::chrono::milliseconds::zero()) {
                    // Leaves the semaphore in the waiting state, which is
                    // guaranteed to incur and additional wake call.
                    return false;
                }
                next = -1;
                detail::concurrency::wait_for(&_value, prev, dt);
                prev = _value.load(std::memory_order_relaxed);
            }
        }
    }

private:
    //  1 = available
    //  0 = unavailable, no wait
    // -1 = unavailable, waiting
    alignas(std::size_t) std::atomic_int _value;
};

using binary_semaphore = counting_semaphore<1>;

}

#endif//JJC_SEMAPHORE_HPP