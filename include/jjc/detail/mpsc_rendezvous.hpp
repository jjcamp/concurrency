#ifndef JJC_DETAIL_MPSC_RENDEZVOUZ_HPP
#define JJC_DETAIL_MPSC_RENDEZVOUZ_HPP

#include <atomic>
#include <chrono>
#include <jjc/detail/mpsc_common.hpp>
#include <jjc/event.hpp>
#include <jjc/mutex.hpp>
#include <mutex>
#include <optional>

namespace jjc::mpsc::detail {

// Basic algo:
// * mutex starts locked
// * a single sender acquires the mutex, emplaces the item
// * receiver unlocks mutex, waits on a flag that item is available, locks the
//   mutex and take the item
// * disconnecting sets the flag without placing an item
// * an additional mutex ensures that only one sender can ever be attempting the
//   rendezvous at a time
template<typename T>
struct rendezvous_channel : detail::sender<T>, detail::receiver<T> {
    rendezvous_channel() {
        _shared.item_lock.lock();
    }

    blocking send_blocks() final { return blocking::ALWAYS; }
    blocking recv_blocks() final { return blocking::ALWAYS; }

    recv_result<T> receive() final {
        _shared.item_lock.unlock();
        _shared.item_ready.wait();
        _shared.item_lock.lock();
        _shared.can_leave.signal();
        auto item = std::exchange(_shared.item, {});
        if (item) {
            return { std::move(*item) };
        }
        else {
            // unlock to make more generic over the mutex type
            _shared.item_lock.unlock();
            return { status::CLOSED };
        }
    }

    recv_result<T> try_receive() final {
        return { status::WOULD_BLOCK };
    }

    recv_result<T> try_receive_until(const std::chrono::steady_clock::time_point& tp) final {
        _shared.item_lock.unlock();
        const auto signaled = _shared.item_ready.wait_until(tp);
        _shared.item_lock.lock();
        _shared.can_leave.signal();
        if (!signaled) return { status::TIMEOUT };
        auto item = std::exchange(_shared.item, {});
        if (item) {
            return { std::move(*item) };
        }
        else {
            _shared.item_lock.unlock();
            return { status::CLOSED };
        }
    }

    send_result<T> send(T&& v) final {
        if (!_shared.open.load(std::memory_order_acquire)) return { status::CLOSED, std::move(v) };

        const auto lk1 = std::scoped_lock(_producer.throttle);
        {
            const auto lk2 = std::scoped_lock(_shared.item_lock);
            _shared.item = std::move(v);
        }
        _shared.item_ready.signal();
        _shared.can_leave.wait();
        return { status::OK, {} };
    }

    send_result<T> try_send(T&& v) final {
        // It's of questionable utility to check to see if the channel is open
        // first when the call will always fail, but it does make behavior more
        // predictable.
        if (!_shared.open.load(std::memory_order_acquire)) return { status::CLOSED, std::move(v) };
        return { status::WOULD_BLOCK, std::move(v) };
    }

    send_result<T> try_send_until(T&& v, const std::chrono::steady_clock::time_point& tp) final {
        if (!_shared.open.load(std::memory_order_acquire)) return { status::CLOSED, std::move(v) };

        if (!_producer.throttle.try_lock_until(tp)) return { status::TIMEOUT, std::move(v) };
        const auto lk1 = std::scoped_lock(std::adopt_lock, _producer.throttle);
        {
            if (!_shared.item_lock.try_lock_until(tp)) return { status::TIMEOUT, std::move(v) };
            const auto lk2 = std::scoped_lock(std::adopt_lock, _shared.item_lock);
            _shared.item = std::move(v);
        }
        _shared.item_ready.signal();
        _shared.can_leave.wait();
        return { status::OK, {} };
    }

    void connect() final {
        _producer.count.fetch_add(1, std::memory_order_relaxed);
    }

    void disconnect() final {
        if (1 == _producer.count.fetch_sub(1, std::memory_order_acq_rel)) {
            _shared.item_ready.signal();
        }
    }

    void close() final {
        _shared.open.store(false, std::memory_order_release);
    }

private:
    struct producer {
        mutex throttle = {};
        std::atomic_ptrdiff_t count = { 1 };
    };

    struct shared {
        std::atomic_bool open = { true };
        mutex item_lock = {};
        event item_ready = {};
        std::optional<T> item = {};
        event can_leave = {}; // need to re-think, don't really want this here
    };

    alignas(detail::cache_alignment) producer _producer = {};
    alignas(detail::cache_alignment) shared _shared = {};
};

}

#endif//JJC_DETAIL_MPSC_RENDEZVOUZ_HPP