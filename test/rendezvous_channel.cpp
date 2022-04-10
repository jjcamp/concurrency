#include <jjc/channel.hpp>
#include <catch2/catch.hpp>

#include <algorithm>
#include "assert_thread.hpp"
#include "channel_test_help.hpp"
#include <future>
#include <memory>

TEST_CASE("rendezvous channel type agnostic", "[mpsc]") {    
    auto [send, recv] = jjc::mpsc::channel<int>(0);

    SECTION("one sender disconnect") {
        {
            auto s = std::move(send);
        }

        REQUIRE(jjc::mpsc::status::CLOSED == recv.receive().result);
    }

    SECTION("receiver closes") {
        {
            auto r = std::move(recv);
        }

        REQUIRE(jjc::mpsc::status::CLOSED == send.send(42));
    }
}

TEMPLATE_TEST_CASE("rendezvous channel", "[mpsc]", int, std::unique_ptr<int>) {
    auto [send, recv] = jjc::mpsc::channel<TestType>(0);

    SECTION("always blocks") {

        REQUIRE(jjc::mpsc::blocking::ALWAYS == send.blocks());
        REQUIRE(jjc::mpsc::blocking::ALWAYS == recv.blocks());

        auto should_fail = send.try_send(PUT(42));
        REQUIRE(jjc::mpsc::status::WOULD_BLOCK == should_fail);
        REQUIRE(42 == GET(std::move(should_fail.item.value())));
        REQUIRE(jjc::mpsc::status::WOULD_BLOCK == recv.try_receive().result);
    }

    SECTION("single sender rendezvous with receiver") {
        auto t = std::async(std::launch::async, [send = std::move(send)]() mutable {
            REQUIRE_T(send.send(PUT(42)));
        });
        
        REQUIRE(42 == GET(recv.receive().value()));
    }

    SECTION("multiple senders can rendezvous") {
        auto s1 = std::move(send);
        auto s2 = s1;

        auto t2 = std::async(std::launch::async, [s1 = std::move(s1)]() mutable {
            auto s = std::move(s1);
            s.send(PUT(1));
            s.send(PUT(2));
        });
        auto t3 = std::async(std::launch::async, [s2 = std::move(s2)]() mutable {
            auto s = std::move(s2);
            s.send(PUT(3));
            s.send(PUT(4));
        });

        auto received = std::vector<TestType>();
        for (auto&& value : recv) {
            received.push_back(std::move(value));
        }

        REQUIRE(4 == received.size());
    }
}