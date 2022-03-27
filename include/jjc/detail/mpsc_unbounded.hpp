#ifndef JJC_DETAIL_MPSC_UNBOUNDED_HPP
#define JJC_DETAIL_MPSC_UNBOUNDED_HPP

#include <atomic>
#include <jjc/detail/mpsc_common.hpp>
#include <jjc/event.hpp>
#include <jjc/mutex.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>

namespace jjc::mpsc::detail {

// Implements a common fifo algorithm which keeps a "buffer" item at the head of
// the queue to prevent requiring synchronization of the head pointer.
// Notifying the receiver of sender disconnects is as easy as sending an node
// with no item, ensuring the receiver processes all current items before
// closing itself.
template<typename T>
struct unbounded_channel : detail::sender<T>, detail::receiver<T> {
    unbounded_channel() :
        _consumer { new node() },
        _producer { _consumer.first }
    {}

    ~unbounded_channel() {
        while (_consumer.first != nullptr) {
            delete std::exchange(_consumer.first, _consumer.first->next.load(std::memory_order_relaxed));
        }
    }

    unbounded_channel(const unbounded_channel&) = delete;
    unbounded_channel& operator=(const unbounded_channel&) = delete;

    blocking send_blocks() final { return blocking::NEVER; }
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

        auto* n = new node(std::move(v));
        // The tail is updated first, allowing the thread that accessed it
        // exclusive (producer-side) access to the node.
        auto* last = _producer.last.load(std::memory_order_relaxed);
        while (!_producer.last.compare_exchange_weak(last, n, std::memory_order_acq_rel, std::memory_order_relaxed)) {}
        last->next.store(n, std::memory_order_release);

        // Note: the thread that first "acquires" the tail may not be the first
        // to signal. In theory that could lead to an unfortunate spurious wake
        // for the consumer. In practice the wake takes time, so first->next is
        // all but guaranteed to be populated.
        _shared.ready.signal();
        return { status::OK, {} };
    }

    void connect() final {
        _producer.count.fetch_add(1, std::memory_order_relaxed);
    }

    void disconnect() final {
        if (1 == _producer.count.fetch_sub(1, std::memory_order_acq_rel)) {
            auto* n = new node();
            _producer.last.load(std::memory_order_relaxed)->next.store(n, std::memory_order_release);
            // _producer.last is never touched again, so it is left dangling
            _shared.ready.signal();
        }
    }

    void close() final {
        _shared.open.store(false, std::memory_order_release);
    }

private:
    recv_result<T> pop() {
        std::unique_ptr<node> t{std::exchange(_consumer.first, _consumer.first->next.load(std::memory_order_relaxed))};
        if (auto& out = _consumer.first->value) {
            return { std::move(*out) };
        }
        return { status::CLOSED };
    }

    struct node {
        explicit node() = default;
        explicit node(T&& v) : value(std::move(v)) {}
        std::optional<T> value = {};
        std::atomic<node*> next = { nullptr };
    };

    struct consumer {
        node* first;
    };

    struct shared {
        std::atomic_bool open = { true };
        event ready = {};
    };

    struct producer {
        std::atomic<node*> last;
        std::atomic_ptrdiff_t count = { 1 };
    };

    alignas(detail::cache_alignment) consumer _consumer;
    alignas(detail::cache_alignment) shared _shared;
    alignas(detail::cache_alignment) producer _producer;
};

}

#endif//JJC_DETAIL_MPSC_UNBOUNDED_HPP