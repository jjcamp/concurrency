#include <jjc/event.hpp>
#include <catch2/catch.hpp>

#include <array>
#include <atomic>
#include <jjc/latch.hpp>
#include <thread>

TEST_CASE("event", "[primitive]") {
    using namespace std::chrono_literals;
    jjc::event e { false };

    SECTION("basic invariants") {
        e.signal();
        e.wait();
        REQUIRE(!e.wait_for(1ms));
        e.signal();
        REQUIRE(e.wait_until(std::chrono::steady_clock::now() + 1ms));
    }

    SECTION("multiple waiters") {
        constexpr auto total = 5;
        jjc::latch l { total + 1 };
        std::atomic_int count = 0;

        std::array<std::thread, total> threads {};
        for (auto& t : threads) t = std::thread([&] {
            l.count_down(1);
            e.wait();
            count.fetch_add(1, std::memory_order_relaxed);
        });

        l.arrive_and_wait();
        REQUIRE(0 == count);
        e.signal();
        for (auto& t : threads) t.join();
        REQUIRE(total == count);
    }

    SECTION("can wait in stages") {
        constexpr auto num_threads = 5;
        constexpr auto num_stages = 5;

        struct stage {
            std::atomic_int count;
            jjc::latch l;

            stage() : count(0), l(num_threads + 1) {}
        };

        std::array<stage, num_stages> stages {};

        std::array<std::thread, num_threads> threads {};
        for (auto& t : threads) t = std::thread([&] {
            for (auto i = 0; i < num_stages; ++i) {
                stages[i].l.count_down();
                e.wait();
                stages[i].count.fetch_add(1, std::memory_order_relaxed);
            }
        });

        stages[0].l.arrive_and_wait();
        e.signal();
        for (auto i = 1; i < num_stages; ++i) {
            stages[i].l.arrive_and_wait();
            e.signal();
            CHECK(num_threads == stages[i - 1].count);
        }

        for (auto& t : threads) t.join();
        REQUIRE(num_threads == stages[num_stages - 1].count);
    }
}