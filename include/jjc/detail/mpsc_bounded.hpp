#ifndef JJC_CONCURRENCY_DETAIL_MPSC_BOUNDED_HPP
#define JJC_CONCURRENCY_DETAIL_MPSC_BOUNDED_HPP

#include <atomic>
#include <chrono>
#include <jjc/detail/mpsc_common.hpp>
#include <jjc/event.hpp>
#include <jjc/mutex.hpp>
#include <jjc/semaphore.hpp>
#include <mutex>
#include <optional>

namespace jjc::mpsc::detail {

// Very similar to the unbounded channel, but with a second FIFO of available
// nodes.
// Note that the total number of nodes is capacity + 2, and disconnecting all
// receivers may add another node (disconnecting can never block).
template<typename T>
struct bounded_channel : detail::sender<T>, detail::receiver<T> {
    bounded_channel(std::size_t capacity) :
        _consumer(),
        _shared(capacity),
        _producer { _consumer.first }
    {
        auto* p = new node();
        _producer.available.store(p, std::memory_order_relaxed);
        for (std::size_t i = 0; i < capacity; ++i) {
            auto* n = new node();
            p->next.store(n, std::memory_order_relaxed);
            p = n;
        }
        _consumer.retired = p;
    }

    ~bounded_channel() {
        while (_consumer.first != nullptr) {
            delete std::exchange(_consumer.first, _consumer.first->next.load(std::memory_order_relaxed));
        }
        for (auto* n = _producer.available.load(std::memory_order_relaxed); n != nullptr;) {
            delete std::exchange(n, n->next.load(std::memory_order_relaxed));
        }
    }

    bounded_channel(const bounded_channel&) = delete;
    bounded_channel& operator =(const bounded_channel&) = delete;

    blocking send_blocks() final { return blocking::SOMETIMES; }
    blocking recv_blocks() final { return blocking::SOMETIMES; }

    recv_result<T> receive() final {
        while (_consumer.first->next.load(std::memory_order_acquire) == nullptr) {
            _shared.ready.wait();
        }
        return pop();
    }

    recv_result<T> try_receive() final {
        if (_consumer.first->next.load(std::memory_order_acquire) != nullptr) {
            return pop();
        }
        else return { status::WOULD_BLOCK };
    }

    recv_result<T> try_receive_until(const std::chrono::steady_clock::time_point& tp) final {
        while (_consumer.first->next.load(std::memory_order_acquire) == nullptr) {
            if (!_shared.ready.wait_until(tp)) return { status::TIMEOUT };
        }
        return pop();
    }

    send_result<T> send(T&& v) final {
        if (!_shared.open.load(std::memory_order_acquire)) return { status::CLOSED, std::move(v) };

        _shared.producer_sem.acquire();
        return push(std::move(v));
    }

    send_result<T> try_send(T&& v) final {
        if (!_shared.open.load(std::memory_order_acquire)) return { status::CLOSED, std::move(v) };

        if (!_shared.producer_sem.try_acquire()) return { status::WOULD_BLOCK, std::move(v) };
        return push(std::move(v));
    }

    send_result<T> try_send_until(T&& v, const std::chrono::steady_clock::time_point& tp) final {
        if (!_shared.open.load(std::memory_order_acquire)) return { status::CLOSED, std::move(v) };

        if (!_shared.producer_sem.try_acquire_until(tp)) return { status::TIMEOUT, std::move(v) };
        return push(std::move(v));
    }

    void connect() final {
        _producer.count.fetch_add(1, std::memory_order_relaxed);
    }

    void disconnect() final {
        if (1 == _producer.count.fetch_sub(1, std::memory_order_acq_rel)) {
            node* n = nullptr;
            // re-use a node if available
            if (_shared.producer_sem.try_acquire()) {
                n = _producer.available.load(std::memory_order_relaxed);
                _producer.available.store(n->next.load(std::memory_order_acquire), std::memory_order_relaxed);
                recycle(n, {});
            }
            // but don't block
            else {
                n = new node();
            }
            _producer.last.load(std::memory_order_relaxed)->next.store(n, std::memory_order_release);
            _shared.ready.signal();
        }
    }

    void close() final {
        _shared.open.store(false, std::memory_order_release);
        // Unblock producers. It may be worth taking advantage of the specific
        // semaphore implementation to just release(num_waiters).
        auto* n = _consumer.first;
        if (std::exchange(n, n->next.load(std::memory_order_acquire)) != nullptr) {
            _shared.producer_sem.release(1);
        }
    }

private:
    recv_result<T> pop() {
        auto* const t = std::exchange(_consumer.first, _consumer.first->next.load(std::memory_order_relaxed));
        t->next.store(nullptr, std::memory_order_relaxed);
        _consumer.retired->next.store(t, std::memory_order_release);
        _shared.producer_sem.release();
        _consumer.retired = t;
        if (auto& out = _consumer.first->value) {
            return { std::move(*out) };
        }
        else return { status::CLOSED };
    }

    send_result<T> push(T&& v) {
        auto* n = _producer.available.load(std::memory_order_relaxed);
        for (
            auto* next = n->next.load(std::memory_order_acquire);
            !_producer.available.compare_exchange_strong(n, next, std::memory_order_acq_rel, std::memory_order_relaxed);
            next = n->next.load(std::memory_order_acquire)
        ) {}
        recycle(n, std::move(v));
        auto* last = _producer.last.load(std::memory_order_relaxed);
        while(!_producer.last.compare_exchange_weak(last, n, std::memory_order_acq_rel, std::memory_order_relaxed)) {}
        last->next.store(n, std::memory_order_release);
        _shared.ready.signal();
        return { status::OK, {} };
    }

    struct node {
        std::optional<T> value = {};
        std::atomic<node*> next = { nullptr };
    };

    void recycle(node* n, std::optional<T>&& v) {
        n->value = std::move(v);
        n->next.store(nullptr, std::memory_order_relaxed);
    }

    struct consumer {
        node* first = new node();
        node* retired = nullptr;
    };

    struct shared {
        counting_semaphore<> producer_sem;
        event ready = {};
        std::atomic_bool open = { true };

        shared(std::ptrdiff_t capacity) : producer_sem(capacity) {}
    };

    struct producer {
        std::atomic<node*> last;
        std::atomic<node*> available = { nullptr };
        std::atomic_ptrdiff_t count = { 1 };
    };

    alignas(detail::cache_alignment) consumer _consumer;
    alignas(detail::cache_alignment) shared _shared;
    alignas(detail::cache_alignment) producer _producer;
};

}

#endif//JJC_CONCURRENCY_DETAIL_MPSC_BOUNDED_HPP