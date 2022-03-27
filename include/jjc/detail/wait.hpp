#ifndef JJC_DETAIL_CONCURRENCY_WAIT_HPP
#define JJC_DETAIL_CONCURRENCY_WAIT_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <type_traits>

#if !defined(_WIN32) && !defined(__linux__)
#error "System is not currently supported"
#endif

namespace jjc::detail::concurrency {

static_assert(sizeof(int) == sizeof(std::atomic<int>));

template<typename T, typename = void>
struct is_waitable : std::false_type {};

template<typename T>
struct is_waitable<T, std::enable_if_t<4 == sizeof(T) && std::is_trivially_copyable_v<T>>> : std::true_type {};

// return values are system-specific, for debugging purposes only
int wait_impl(void* obj, void* expected) noexcept;
int wait_for_impl(void* obj, void* expected, const std::chrono::milliseconds&) noexcept;
int wake_impl(void* obj, uint32_t count) noexcept;
int wake_all_impl(void* obj) noexcept;

template<typename T>
int wait(T* obj, T expected) noexcept {
    static_assert(is_waitable<T>::value);
    return wait_impl(obj, &expected);
}

template<typename T>
int wait(std::atomic<T>* obj, T expected) {
    static_assert(is_waitable<T>::value);
    return wait_impl(obj, &expected);
}

template<typename T>
int wait_for(T* obj, T expected, const std::chrono::milliseconds& d) noexcept {
    static_assert(is_waitable<T>::value);
    return wait_for_impl(obj, &expected, d);
}

template<typename T>
int wait_for(std::atomic<T>* obj, T expected, const std::chrono::milliseconds& d) {
    static_assert(is_waitable<T>::value);
    return wait_for_impl(obj, &expected, d);
}

template<typename T>
int wake(T* obj, uint32_t count) {
    static_assert(is_waitable<T>::value);
    return wake_impl(obj, count);
}

template<typename T>
int wake(std::atomic<T>* obj, uint32_t count) {
    static_assert(is_waitable<T>::value);
    return wake_impl(obj, count);
}

template<typename T>
int wake_all(T* obj) {
    static_assert(is_waitable<T>::value);
    return wake_all_impl(obj);
}

template<typename T>
int wake_all(std::atomic<T>* obj) {
    static_assert(is_waitable<T>::value);
    return wake_all_impl(obj);
}

}

#endif//JJC_DETAIL_CONCURRENCY_WAIT_HPP