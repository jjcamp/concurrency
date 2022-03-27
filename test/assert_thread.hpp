#pragma once

#include <exception>

// Catch2 assertions are not thread-safe. The bare-minimum impl just needs to
// cause the test to fail and work in release builds.
#define REQUIRE_T(expr) if (!(expr)) throw std::exception()