#include <jjc/detail/wait.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace jjc::detail::concurrency {

int wait_impl(void* obj, void* expected) noexcept {
    return WaitOnAddress(obj, expected, sizeof(int), INFINITE);
}

int wait_for_impl(void* obj, void* expected, const std::chrono::milliseconds& d) noexcept {
    return WaitOnAddress(obj, expected, sizeof(int), static_cast<DWORD>(d.count()));
}

int wake_impl(void* obj, uint32_t count) noexcept {
    if (count > 1) WakeByAddressAll(obj);
    else WakeByAddressSingle(obj);
    return 1;
}

int wake_all_impl(void* obj) noexcept {
    WakeByAddressAll(obj);
    return 1;
}

}