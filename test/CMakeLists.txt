cmake_minimum_required(VERSION 3.16)

add_executable(jjc-concurrency-test)
add_executable(jjc::tests::concurrency ALIAS jjc-concurrency-test)

target_sources(jjc-concurrency-test
    PRIVATE
        bounded_channel.cpp
        event.cpp
        latch.cpp
        mutex.cpp
        rendezvous_channel.cpp
        semaphore.cpp
        unbounded_channel.cpp
)

find_package(Catch2 CONFIG REQUIRED)

target_link_libraries(jjc-concurrency-test
    PRIVATE jjc::concurrency
    PUBLIC Catch2::Catch2
)

if (UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(jjc-concurrency-test
        PUBLIC Threads::Threads
    )
endif()

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/test-main.cpp
    "#define CATCH_CONFIG_MAIN\n"
    "#include <catch2/catch.hpp>"
)

target_sources(jjc-concurrency-test
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/test-main.cpp
)