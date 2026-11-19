cmake_minimum_required(VERSION 3.16)
project(hybrid_omp_sched LANGUAGES CXX)

# ── C++ standard ────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Default build type ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ── Options ──────────────────────────────────────────────────────────────────
option(HS_NATIVE        "Add -march=native (disable for CI/portable builds)"   ON)
option(HS_BUILD_BENCH   "Build hs_bench executable"                             ON)
option(HS_BUILD_TOOLS   "Build hs_power_profile, hs_analyze, hs_decision_guide" ON)
option(HS_BUILD_TESTS   "Build hs_tests"                                        ON)
option(HS_BUILD_EXAMPLES "Build examples"                                       ON)
option(HS_SANITIZE      "Enable AddressSanitizer + UBSan (not compatible with TSan)" OFF)

# ── Find packages ────────────────────────────────────────────────────────────
find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)

# ── Compiler warnings / flags ────────────────────────────────────────────────
set(HS_WARN_FLAGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND HS_WARN_FLAGS -Wall -Wextra -Wpedantic)
    if(HS_NATIVE)
        list(APPEND HS_WARN_FLAGS -march=native)
    endif()
    if(HS_SANITIZE)
        list(APPEND HS_WARN_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Optimisation flags for Release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-O3)
    endif()
endif()

# ── Library: hybrid_sched ────────────────────────────────────────────────────
add_library(hybrid_sched STATIC
    src/util.cpp
    src/topology.cpp
    src/energy.cpp
    src/power_model.cpp
    src/calibrator.cpp
    src/planner.cpp
    src/partition.cpp
    src/loop_state.cpp
    src/hybrid_for.cpp
)
add_library(hybrid_sched::hybrid_sched ALIAS hybrid_sched)

target_include_directories(hybrid_sched PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(hybrid_sched PUBLIC OpenMP::OpenMP_CXX Threads::Threads)
target_compile_options(hybrid_sched PRIVATE ${HS_WARN_FLAGS})

# ── Examples ─────────────────────────────────────────────────────────────────
if(HS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── Benchmarks ───────────────────────────────────────────────────────────────
if(HS_BUILD_BENCH)
    add_subdirectory(benchmarks)
endif()

# ── Tools ────────────────────────────────────────────────────────────────────
if(HS_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
if(HS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
