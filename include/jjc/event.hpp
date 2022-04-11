#ifndef JJC_EVENT_HPP
#define JJC_EVENT_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <jjc/detail/wait.hpp>

namespace jjc {

/**
 * A reusable, automatic reset signal.
 * 
 * Each event has two states: unsignaled and signaled. If a thread is
 * unsignaled, threads that wish to wait on the event join its wait queue. If it
 * is signaled, no wait happens.
 * 
 * When an event is signaled, all waiters are notified. Waiters will only ever
 * block on an event if the previous event was observed.
 */
struct event {
    constexpr event(bool signaled = false) noexcept :
        _value(signaled ? 1 : 0)
    {}

    event(const event&) = delete;
    event& operator =(const event&) = delete;

    void signal() noexcept {
        auto prev = _value.load(std::memory_order_relaxed);
        while (true) {
            auto event = (prev + 1) | 1;
            if (_value.compare_exchange_strong(prev, event, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                break;
            }
        }
        // only wake if the event was unsignaled
        if (!is_signaled(prev)) {
            detail::concurrency::wake_all(&_value);
        }
    }

    void wait() {
        auto prev = _value.load(std::memory_order_acquire);
        if (is_signaled(prev)) {
            if (_value.compare_exchange_strong(prev, prev + 1, std::memory_order_release, std::memory_order_acquire)) {
                return;
            }
            if (is_signaled(prev)) return;
        }

        auto event = prev + 1;
        do {
            detail::concurrency::wait(&_value, prev);
            prev = _value.load(std::memory_order_relaxed);

        } while (prev < event);

        _value.compare_exchange_strong(event, event + 1, std::memory_order_release, std::memory_order_relaxed);
    }

    template<typename Rep, typename Period>
    bool wait_for(const std::chrono::duration<Rep, Period>& d) {
        const auto t = std::chrono::steady_clock::now() + d;
        return wait_until(t);
    }

    template<typename Clock, typename Duration>
    bool wait_until(const std::chrono::time_point<Clock, Duration>& t) {
        auto prev = _value.load(std::memory_order_acquire);
        if (is_signaled(prev)) {
            if (_value.compare_exchange_strong(prev, prev + 1, std::memory_order_release, std::memory_order_acquire)) {
                return true;
            }
            if (is_signaled(prev)) return true;
        }

        auto event = prev + 1;
        do {
            const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t - Clock::now());
            if (dt <= std::chrono::milliseconds::zero()) {
                return false;
            }

            detail::concurrency::wait(&_value, prev);
            prev = _value.load(std::memory_order_relaxed);

        } while (prev < event);

        _value.compare_exchange_strong(event, event + 1, std::memory_order_release, std::memory_order_relaxed);
        return true;
    }

private:
    constexpr bool is_signaled(uint32_t v) {
        return 1 == (v & 1);
    }

    // An incrementing event counter. Odd values indicate a signaled event, even
    // values indicate that the event associated with the next value is
    // unsignaled.
    alignas(std::size_t) std::atomic_uint32_t _value;
};

}

#endif//JJC_EVENT_HPP