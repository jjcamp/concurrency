#include <jjc/semaphore.hpp>
#include <catch2/catch.hpp>

#include <array>
#include "assert_thread.hpp"
#include <atomic>
#include <jjc/latch.hpp>
#include <thread>

TEST_CASE("counting_semaphore", "[primitive]") {
    using namespace std::chrono_literals;

    SECTION("basic invariants") {
        jjc::counting_semaphore s{2};
        s.acquire();
        REQUIRE(s.try_acquire());
        REQUIRE(!s.try_acquire());
        s.release(2);
        REQUIRE(s.try_acquire_for(1ms));
        REQUIRE(s.try_acquire_until(std::chrono::steady_clock::now() + 1ms));
    }

    SECTION("parallel blocking acquire") {
        constexpr auto count = 2;
        jjc::counting_semaphore<count> s{0};
        std::atomic_int res = count;

        const auto worker = [&]() {
            s.acquire();
            const auto v = --res;
            REQUIRE_T(v >= 0 && v < count);
            res.fetch_add(1, std::memory_order_relaxed);
            s.release();
        };

        std::array<std::thread, 5> tasks{};
        for (auto& t : tasks) t = std::thread(worker);

        s.release(count);

        for (auto& t : tasks) t.join();
        REQUIRE(res == count);
    }

    SECTION("parallel non-blocking acquire") {
        constexpr auto num_workers = 15;
        constexpr auto count = 5;
        jjc::counting_semaphore<> s{count};
        std::atomic<int> res = 0;
        jjc::latch latch { num_workers + 1 };

        const auto worker = [&] {
            latch.arrive_and_wait();
            if (s.try_acquire()) res.fetch_add(1, std::memory_order_relaxed);
        };

        std::array<std::thread, num_workers> tasks{};
        for (auto& t : tasks) t = std::thread(worker);

        latch.arrive_and_wait();

        for (auto& t : tasks) t.join();
        REQUIRE(count == res);
    }

    SECTION("parallel timeout acquire") {
        constexpr auto count = 5;
        jjc::counting_semaphore<> s{0};
        std::atomic_int res = 0;

        INFO("This is a time-based test and could have a false failure");
        constexpr auto sleep_time = 100ms;

        const auto worker = [&] {
            if (s.try_acquire_for(sleep_time))
                res.fetch_add(1, std::memory_order_relaxed);
        };

        std::array<std::thread, 15> tasks{};
        for (auto& t : tasks) t = std::thread(worker);

        s.release(count);

        for (auto& t : tasks) t.join();
        CHECK_NOFAIL(res == count);
    }
}