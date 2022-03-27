#include <jjc/mutex.hpp>
#include <catch2/catch.hpp>

#include <array>
#include <mutex>
#include <thread>

TEST_CASE("mutex", "[primitive]") {
    using namespace std::chrono_literals;

    // alignment ensures that synchonization isn't due to the mutex invalidating
    // the protected value's cache line
    alignas(64) int count = 0;
    alignas(64) jjc::mutex m = {};

    SECTION("basic mutual exclusion") {
        constexpr auto total = 10;

        std::array<std::thread, total> threads {};
        for (auto& t : threads) t = std::thread([&] {
            const auto lk = std::scoped_lock(m);
            ++count;
        });
        for (auto& t : threads) t.join();

        REQUIRE(total == count);
    }

    SECTION("(mostly) guaranteed contention") {
        constexpr auto total = 5;

        std::array<std::thread, total> threads {};
        for (auto& t : threads) t = std::thread([&] {
            const auto lk = std::scoped_lock(m);
            std::this_thread::sleep_for(1ms);
            ++count;
        });
        for (auto& t : threads) t.join();

        REQUIRE(total == count);
    }
}