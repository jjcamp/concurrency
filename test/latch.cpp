#include <jjc/latch.hpp>
#include <catch2/catch.hpp>

#include <array>
#include <thread>

TEST_CASE("latch", "[primitive]") {
    SECTION("basic invariants") {
        jjc::latch l1 { 1 };
        l1.count_down(1);
        l1.wait();
        REQUIRE(l1.try_wait());

        jjc::latch l2 { 1 };
        l2.arrive_and_wait();
    }

    SECTION("synchronizes") {
        constexpr auto total = 5;
        jjc::latch l { total };
        
        std::array<std::thread, total> threads {};
        for (auto& t : threads) t = std::thread([&l] {
            l.arrive_and_wait();
        });
        for (auto& t : threads) t.join();
    }
}