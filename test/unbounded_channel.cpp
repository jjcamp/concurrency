#include <jjc/channel.hpp>
#include <catch2/catch.hpp>

#include <array>
#include "channel_test_help.hpp"
#include <future>
#include <jjc/latch.hpp>
#include <memory>
#include <thread>

TEST_CASE("unbounded channel type agnostic", "[mpsc]") {
    SECTION("one sender disconnect") {
        auto [send, recv] = jjc::mpsc::channel<int>();

        {
            auto s = std::move(send);
        }

        REQUIRE(jjc::mpsc::status::CLOSED == recv.receive().result);
    }

    SECTION("receiver closes") {
        auto [send, recv] = jjc::mpsc::channel<int>();

        {
            auto r = std::move(recv);
        }

        REQUIRE(jjc::mpsc::status::CLOSED == send.send(42));
    }
}

TEMPLATE_TEST_CASE("unbounded channel", "[mpsc]", int, std::unique_ptr<int>) {
    SECTION("basic invariants") {
        using namespace std::chrono_literals;
        auto [send, recv] = jjc::mpsc::channel<TestType>();

        REQUIRE(jjc::mpsc::blocking::NEVER == send.blocks());
        REQUIRE(jjc::mpsc::blocking::SOMETIMES == recv.blocks());

        REQUIRE(jjc::mpsc::status::WOULD_BLOCK == recv.try_receive().result);
        REQUIRE(send.send(PUT(42)));
        REQUIRE(42 == GET(recv.receive().value()));
        REQUIRE(send.try_send(PUT(42)));
        REQUIRE(42 == GET(recv.try_receive().value()));
        REQUIRE(send.try_send_for(PUT(42), 1ms));
        REQUIRE(42 == GET(recv.try_receive_for(1ms).value()));
        REQUIRE(send.try_send_until(PUT(42), std::chrono::steady_clock::now() + 1ms));
        REQUIRE(42 == GET(recv.try_receive_until(std::chrono::steady_clock::now() + 1ms).value()));
        REQUIRE(send.try_send_until(PUT(42), std::chrono::system_clock::now() + 1ms));
        REQUIRE(42 == GET(recv.try_receive_until(std::chrono::system_clock::now() + 1ms).value()));
    }

    SECTION("multiple senders (single thread)") {
        auto [send, recv] = jjc::mpsc::channel<TestType>();

        {
            auto s1 = std::move(send);
            auto s2 = s1;

            REQUIRE(s1.send(PUT(1)));
            REQUIRE(s2.send(PUT(2)));
            REQUIRE(1 == GET(recv.receive().value()));
            REQUIRE(s1.send(PUT(3)));
        }

        REQUIRE(2 == GET(recv.receive().value()));
        REQUIRE(3 == GET(recv.receive().value()));
        REQUIRE(jjc::mpsc::status::CLOSED == recv.receive().result);
    }

    SECTION("one sender") {
        auto [send, recv] = jjc::mpsc::channel<TestType>();
        static constexpr auto count = 5;
        jjc::latch latch { 2 };

        auto s = std::async(std::launch::async, [send = std::move(send), &latch]() mutable {
            latch.arrive_and_wait();
            for (auto i = 0; i < count; ++i) {
                send.send(PUT(i));
            }
        });

        latch.arrive_and_wait();
        for (auto i = 0; i < count; ++i) {
            CHECK(i == GET(recv.receive().value()));
        }
    }

    SECTION("multiple senders") {
        auto [send, recv] = jjc::mpsc::channel<TestType>();
        static constexpr auto count = 5;
        static constexpr auto senders = 5;
        jjc::latch latch { senders + 1 };

        std::array<std::thread, senders> threads {};
        {
            auto s = std::move(send);
            for (auto& t : threads) t = std::thread([send = s, &latch]() mutable {
                latch.arrive_and_wait();
                for (auto i = 0; i < count; ++i) {
                    send.try_send(PUT(i));
                }
            });
        }

        auto total = 0;
        latch.arrive_and_wait();
        for ([[maybe_unused]] auto& _ : recv) {
            ++total;
        }

        REQUIRE(count * senders == total);
        for (auto& t : threads) t.join();
    }
}