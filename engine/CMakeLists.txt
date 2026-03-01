cmake_minimum_required(VERSION 3.20)
project(poker_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization flags
if(MSVC)
    add_compile_options(/O2 /W4 /EHsc)
    # AVX2 if supported
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(/arch:AVX2 HAS_AVX2)
    if(HAS_AVX2)
        add_compile_options(/arch:AVX2)
    endif()
else()
    add_compile_options(-O3 -Wall -Wextra -Wpedantic -march=native -flto)
    add_link_options(-flto)
endif()

# Thread support
find_package(Threads REQUIRED)

# ── Engine static library ──────────────────────────────────────────────
set(ENGINE_SOURCES
    card.cpp
    deck.cpp
    hand_evaluator.cpp
    game_state.cpp
    pot_manager.cpp
    information_set.cpp
    infoset_store.cpp
    mccfr.cpp
    trainer.cpp
    card_abstraction.cpp
    equity_calculator.cpp
    action_abstraction.cpp
    utils.cpp
)

add_library(poker_engine_lib STATIC ${ENGINE_SOURCES})
target_include_directories(poker_engine_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(poker_engine_lib PUBLIC Threads::Threads)

# ── Solver executable ─────────────────────────────────────────────────
add_executable(poker_solver main.cpp)
target_link_libraries(poker_solver PRIVATE poker_engine_lib)

# ── Google Test via FetchContent ──────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# Prevent overriding parent project compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tests/engine)

set(TEST_SOURCES
    ${TEST_DIR}/test_card.cpp
    ${TEST_DIR}/test_hand_evaluator.cpp
    ${TEST_DIR}/test_game_state.cpp
    ${TEST_DIR}/test_pot_manager.cpp
    ${TEST_DIR}/test_mccfr.cpp
    ${TEST_DIR}/test_abstraction.cpp
)

add_executable(poker_tests ${TEST_SOURCES})
target_link_libraries(poker_tests PRIVATE poker_engine_lib GTest::gtest_main)
target_include_directories(poker_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(poker_tests)

# ── Postflop bucketing tests (heavy, runs as single CTest entry) ────
add_executable(postflop_tests ${TEST_DIR}/test_postflop_bucketing.cpp)
target_link_libraries(postflop_tests PRIVATE poker_engine_lib GTest::gtest_main)
target_include_directories(postflop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME PostflopBucketing COMMAND postflop_tests)
