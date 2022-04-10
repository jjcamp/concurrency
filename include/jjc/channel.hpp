#ifndef JJC_CONCURRENCY_CHANNEL_HPP
#define JJC_CONCURRENCY_CHANNEL_HPP

#include <chrono>
#include <jjc/detail/mpsc_common.hpp>
#include <jjc/detail/mpsc_bounded.hpp>
#include <jjc/detail/mpsc_rendezvous.hpp>
#include <jjc/detail/mpsc_unbounded.hpp>
#include <limits>
#include <memory>
#include <optional>

namespace jjc::mpsc {

template<typename T>
struct sender;

template<typename T>
struct receiver;

inline constexpr std::ptrdiff_t unbounded = -1;

struct invalid_capacity : std::logic_error {
    invalid_capacity() : std::logic_error("invalid channel capacity") {}
};

/**
 * Creates a multi-producer, single-consumer FIFO for cross-thread message
 * passing.
 * 
 * The capacity determines the resulting channel type:
 * 
 * * jjc::mpsc::unbounded - A fully asynchronous channel. Sends never block, but
 *     the queue may grow arbitrarily large if the receiver cannot process items
 *     quick enough.
 * * 0 - A fully synchronous channel. Every send blocks in order to "rendezvous"
 *     with a receive
 * * > 0 - Mixed synchronicity. Sends will block *iff* there are already
 *     `capacity` items in the queue
 * * Anything else - throws jjc::mpsc::invalid_capacity
 * 
 * @param capacity jjc::mpsc::unbounded by default
 * @returns sender/receiver pair
 */
template<typename T>
auto channel(std::ptrdiff_t capacity = unbounded) -> std::pair<sender<T>, receiver<T>>;

template<typename T>
struct sender {
    send_result<T> send(T&& v) {
        return _channel->send(std::move(v));
    }

    send_result<T> send(const T& v) {
        return _channel->send(v);
    }

    send_result<T> try_send(T&& v) {
        return _channel->try_send(std::move(v));
    }

    send_result<T> try_send(const T& v) {
        return _channel->try_send(v);
    }

    template<typename Rep, typename Period>
    send_result<T> try_send_for(T&& v, const std::chrono::duration<Rep, Period>& timeout_after) {
        const auto tp = std::chrono::steady_clock::now() + timeout_after;
        return _channel->try_send_until(std::move(v), tp);
    }

    template<typename Rep, typename Period>
    send_result<T> try_send_for(const T& v, const std::chrono::duration<Rep, Period>& timeout_after) {
        const auto tp = std::chrono::steady_clock::now() + timeout_after;
        return _channel->try_send_until(v, tp);
    }

    template<typename Clock, typename Duration>
    send_result<T> try_send_until(T&& v, const std::chrono::time_point<Clock, Duration>& timeout_at) {
        const auto d = timeout_at - Clock::now();
        return try_send_for(std::move(v), d);
    }

    send_result<T> try_send_until(T&& v, const std::chrono::steady_clock::time_point& timeout_at) {
        return _channel->try_send_until(std::move(v), timeout_at);
    }

    template<typename Clock, typename Duration>
    send_result<T> try_send_until(const T& v, const std::chrono::time_point<Clock, Duration>& timeout_at) {
        const auto d = timeout_at - Clock::now();
        return try_send_for(v, d);
    }

    send_result<T> try_send_until(const T& v, const std::chrono::steady_clock::time_point& timeout_at) {
        return _channel->try_send_until(v, timeout_at);
    }

    blocking blocks() const noexcept {
        return _channel->send_blocks();
    }

    ~sender() {
        if (_channel) _channel->disconnect();
    }

    sender(sender&&) noexcept = default;
    sender& operator=(sender&&) noexcept = default;

    sender(const sender& other) noexcept :
        _channel(other._channel)
    {
        _channel->connect();
    }

    sender& operator=(const sender& rhs) noexcept {
        _channel = rhs._channel;
        _channel->connect();
        return *this;
    }

private:
    friend auto channel<T>(std::ptrdiff_t) -> std::pair<sender<T>, receiver<T>>;

    explicit sender(std::shared_ptr<detail::sender<T>> ch) :
        _channel(std::move(ch))
    {}

    std::shared_ptr<detail::sender<T>> _channel;
};

template<typename T>
struct receiver {
    recv_result<T> receive() {
        return _channel->receive();
    }

    recv_result<T> try_receive() {
        return _channel->try_receive();
    }

    template<typename Rep, typename Period>
    recv_result<T> try_receive_for(const std::chrono::duration<Rep, Period>& timeout_after) {
        const auto tp = std::chrono::steady_clock::now() + timeout_after;
        return _channel->try_receive_until(tp);
    }

    template<typename Clock, typename Duration>
    recv_result<T> try_receive_until(const std::chrono::time_point<Clock, Duration>& timeout_at) {
        const auto d = timeout_at - Clock::now();
        return try_receive_for(d);
    }

    recv_result<T> try_receive_until(const std::chrono::steady_clock::time_point& timeout_at) {
        return _channel->try_receive_until(timeout_at);
    }

    blocking blocks() const noexcept {
        return _channel->recv_blocks();
    }

    ~receiver() {
        if (_channel) _channel->close();
    }

    receiver(receiver&&) noexcept = default;
    receiver(const receiver&) = delete;
    receiver& operator=(receiver&&) noexcept = default;
    receiver& operator=(const receiver&) = delete;

    struct iterator {
        T& operator *() { return *_result; }
        iterator& operator++() { _result = _channel->receive(); return *this; }
        bool operator!=(const iterator& rhs) { return _result.has_value() || rhs._result.has_value(); }

    private:
        friend iterator begin(receiver& r);
        friend constexpr iterator end(const receiver&) noexcept;
        constexpr iterator() noexcept = default;
        constexpr iterator(detail::receiver<T>* channel, recv_result<T> result) noexcept :
            _channel(channel),
            _result(std::move(result))
        {}
        detail::receiver<T>* _channel = nullptr;
        recv_result<T> _result = { status::CLOSED };
    };

private:
    friend auto channel<T>(std::ptrdiff_t) -> std::pair<sender<T>, receiver<T>>;

    friend iterator begin(receiver& r) {
        auto item = r.receive();
        return { r._channel.get(), std::move(item) };
    }

    friend constexpr iterator end(const receiver&) noexcept {
        return {};
    }

    explicit receiver(std::shared_ptr<detail::receiver<T>> ch) :
        _channel(std::move(ch))
    {}

    std::shared_ptr<detail::receiver<T>> _channel;
};

template<typename T>
auto channel(std::ptrdiff_t capacity) -> std::pair<sender<T>, receiver<T>> {
    if (capacity == unbounded) {
        auto chs = std::make_shared<detail::unbounded_channel<T>>();
        auto chr = chs;
        return { sender<T>(std::move(chs)), receiver<T>(std::move(chr)) };
    }
    else if (capacity == 0) {
        auto chs = std::make_shared<detail::rendezvous_channel<T>>();
        auto chr = chs;
        return { sender<T>(std::move(chs)), receiver<T>(std::move(chr)) };
    }
    else if (capacity > 0) {
        auto chs = std::make_shared<detail::bounded_channel<T>>(capacity);
        auto chr = chs;
        return { sender<T>(std::move(chs)), receiver<T>(std::move(chr)) };
    }
    throw invalid_capacity();
}

}

#endif//JJC_CONCURRENCY_CHANNEL_HPP