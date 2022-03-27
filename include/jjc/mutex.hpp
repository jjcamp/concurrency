#ifndef JJC_MUTEX_HPP
#define JJC_MUTEX_HPP

#include <chrono>
#include <jjc/semaphore.hpp>

namespace jjc {

struct mutex {
    constexpr mutex() noexcept = default;
    mutex(const mutex&) = delete;

    void unlock() { _sem.release(); }
    void lock() { _sem.acquire(); }
    bool try_lock() { return _sem.try_acquire(); }

    template<typename Rep, typename Period>
    bool try_lock_for(const std::chrono::duration<Rep, Period>& d) {
        return _sem.try_acquire_for(d);
    }

    template<typename Clock, typename Duration>
    bool try_lock_until(const std::chrono::time_point<Clock, Duration>& t) {
        return _sem.try_acquire_until(t);
    }

private:
    binary_semaphore _sem = { 1 };
};

}

#endif//JJC_MUTEX_HPP