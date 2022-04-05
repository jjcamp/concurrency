cmake_minimum_required(VERSION 3.16)
enable_testing()

project(jjc-concurrency)

option(BUILD_TESTS "Configure test targets" ON)

add_library(jjc-concurrency)
add_library(jjc::concurrency ALIAS jjc-concurrency)

target_include_directories(jjc-concurrency
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(jjc-concurrency
    PUBLIC cxx_std_17
)

if (WIN32)
    target_sources(jjc-concurrency
        PRIVATE src/wait_win.cpp
    )
    target_link_libraries(jjc-concurrency
        PRIVATE Synchronization
    )
elseif ("Linux" STREQUAL CMAKE_SYSTEM_NAME)
    target_sources(jjc-concurrency
        PRIVATE src/wait_linux.cpp
    )
    target_link_libraries(jjc-concurrency
        INTERFACE atomic
    )
elseif (APPLE)
    target_sources(jjc-concurrency
        PRIVATE src/wait_darwin.cpp
    )
endif()

if (${BUILD_TESTS})
    add_subdirectory(test)
endif()