#pragma once

#include <memory>

template<typename T>
auto put(int) -> T;

template<>
inline auto put(int v) -> int { return v; }

template<>
inline auto put(int v) -> std::unique_ptr<int> {
    return std::make_unique<int>(v);
}

#define PUT(v) put<TestType>(v)

template<typename T>
auto get(T&&) -> int;

template<>
inline auto get(int&& v) -> int { return v; }

template<>
inline auto get(std::unique_ptr<int>&& v) -> int { return *v; }

#define GET(v) get<TestType>(v)