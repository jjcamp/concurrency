#ifndef JJC_DETAIL_MPSC_COMMON_HPP
#define JJC_DETAIL_MPSC_COMMON_HPP

#include <chrono>
#include <new>
#include <optional>

namespace jjc::mpsc {

enum class status {
    OK, WOULD_BLOCK, TIMEOUT, CLOSED
};

template<typename T>
struct send_result {
    status result;
    std::optional<T> item;

    explicit operator bool() const { return result == status::OK; }
};

template<typename T>
bool operator==(status s, const send_result<T>& r) {
    return s == r.result;
}

template<typename T>
bool operator==(const send_result<T>& r, status s) {
    return s == r;
}

template<typename T>
struct recv_result : std::optional<T> {
    status result;

    recv_result(T&& v) : std::optional<T>(std::move(v)), result(status::OK) {}
    recv_result(status s) : std::optional<T>(), result(s) {}
};

enum class blocking {
    NEVER, SOMETIMES, ALWAYS
};

}

namespace jjc::mpsc::detail {

#if defined(__cpp_lib_hardware_interference_size)
static constexpr auto cache_alignment = std::hardware_destructive_interference_size;
#else
static constexpr auto cache_alignment = 64;
#endif

template<typename T>
struct sender {
    virtual ~sender() = default;
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual blocking send_blocks() = 0;

    virtual send_result<T> send(T&&) = 0;

    virtual send_result<T> try_send(T&& v) {
        return send(std::move(v));
    }

    virtual send_result<T> try_send_until(T&& v, const std::chrono::steady_clock::time_point&) {
        return send(std::move(v));
    }

    // Remove any stored value from send_result, because:
    // a. users shouldn't care if they're already copying
    // b. the impl could defer the copy (don't allow users to rely on a value
    //    being returned)

    send_result<T> send(const T& v) {
        auto r = send(std::move(T(v)));
        r.item.reset();
        return r;
    }

    send_result<T> try_send(const T& v) {
        auto r = try_send(std::move(T(v)));
        r.item.reset();
        return r;
    }

    send_result<T> try_send_until(const T& v, const std::chrono::steady_clock::time_point& tp) {
        auto r = try_send_for(std::move(T(v)), tp);
        r.item.reset();
        return r;
    }
};

template<typename T>
struct receiver {
    virtual ~receiver() = default;
    virtual void close() = 0;
    virtual blocking recv_blocks() = 0;
    virtual recv_result<T> receive() = 0;
    virtual recv_result<T> try_receive() = 0;
    virtual recv_result<T> try_receive_until(const std::chrono::steady_clock::time_point& tp) = 0;
};

}

#endif//JJC_DETAIL_MPSC_COMMON_HPP