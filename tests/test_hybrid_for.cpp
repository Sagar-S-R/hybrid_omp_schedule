/// @file test_hybrid_for.cpp
/// Tests for hybrid_for: every iteration exactly once, all policies, edge cases.

#include "minitest.hpp"
#include "hybrid_sched/hybrid_for.hpp"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>

// Helper: run hybrid_for with a given policy and verify every index in [0,N) hit exactly once.
static void check_exactly_once(std::size_t N, hs::Policy policy,
                                std::size_t grain = 1,
                                hs::Recalibrate recalib = hs::Recalibrate::EVERY_CALL) {
    std::vector<std::atomic<int>> hits(N);
    for (auto& h : hits) h.store(0, std::memory_order_relaxed);

    hs::Options opts;
    opts.policy = policy;
    opts.grain = grain;
    opts.recalibrate = recalib;
    opts.min_parallel_seconds = 0.0; // always parallel

    hs::reset_loop_state();
    hs::hybrid_for("test.loop", 0, N,
                   [&](std::size_t i) {
                       hits[i].fetch_add(1, std::memory_order_relaxed);
                   },
                   opts);

    for (std::size_t i = 0; i < N; ++i) {
        if (hits[i].load() != 1) {
            std::string msg = "N=" + std::to_string(N) +
                              " i=" + std::to_string(i) +
                              " hits=" + std::to_string(hits[i].load());
            throw minitest::FailException{msg};
        }
    }
}

// ── Sizes ─────────────────────────────────────────────────────────────────────

TEST("hybrid_for: N=0 does nothing") {
    hs::reset_loop_state();
    int called = 0;
    hs::hybrid_for("test.zero", 0, 0, [&](std::size_t) { ++called; });
    CHECK_EQ(called, 0);
}

TEST("hybrid_for: N=1 exactly once") {
    check_exactly_once(1, hs::Policy::EDP);
}

TEST("hybrid_for: N=2 exactly once") {
    check_exactly_once(2, hs::Policy::EDP);
}

TEST("hybrid_for: N=7 exactly once") {
    check_exactly_once(7, hs::Policy::EDP);
}

TEST("hybrid_for: N=63 exactly once") {
    check_exactly_once(63, hs::Policy::EDP);
}

TEST("hybrid_for: N=64 exactly once") {
    check_exactly_once(64, hs::Policy::EDP);
}

TEST("hybrid_for: N=65 exactly once") {
    check_exactly_once(65, hs::Policy::EDP);
}

TEST("hybrid_for: N=1000 exactly once") {
    check_exactly_once(1000, hs::Policy::EDP);
}

TEST("hybrid_for: N=1000000 exactly once") {
    check_exactly_once(1000000, hs::Policy::EDP);
}

// ── Policies ─────────────────────────────────────────────────────────────────

TEST("hybrid_for: policy TIME_BALANCED exactly once N=1000") {
    check_exactly_once(1000, hs::Policy::TIME_BALANCED);
}

TEST("hybrid_for: policy P_ONLY exactly once N=1000") {
    check_exactly_once(1000, hs::Policy::P_ONLY);
}

TEST("hybrid_for: policy E_ONLY exactly once N=1000") {
    check_exactly_once(1000, hs::Policy::E_ONLY);
}

TEST("hybrid_for: policy FIXED_RATIO exactly once N=1000") {
    check_exactly_once(1000, hs::Policy::FIXED_RATIO);
}

TEST("hybrid_for: policy ENERGY exactly once N=1000") {
    check_exactly_once(1000, hs::Policy::ENERGY);
}

// ── Grains ────────────────────────────────────────────────────────────────────

TEST("hybrid_for: grain=7 N=63 exactly once") {
    check_exactly_once(63, hs::Policy::EDP, 7);
}

TEST("hybrid_for: grain=16 N=1000 exactly once") {
    check_exactly_once(1000, hs::Policy::EDP, 16);
}

TEST("hybrid_for: grain=64 N=10000 exactly once") {
    check_exactly_once(10000, hs::Policy::EDP, 64);
}

// ── Recalibration modes ───────────────────────────────────────────────────────

TEST("hybrid_for: recalib=ONCE exactly once over 5 calls") {
    std::vector<std::atomic<int>> hits(500);
    for (auto& h : hits) h.store(0, std::memory_order_relaxed);

    hs::Options opts;
    opts.policy = hs::Policy::EDP;
    opts.recalibrate = hs::Recalibrate::ONCE;
    opts.min_parallel_seconds = 0.0;

    hs::reset_loop_state();
    for (int rep = 0; rep < 5; ++rep) {
        // Each call covers a non-overlapping range of 100
        const std::size_t base = static_cast<std::size_t>(rep) * 100;
        hs::hybrid_for("test.once", base, base + 100,
                       [&](std::size_t i) {
                           hits[i].fetch_add(1, std::memory_order_relaxed);
                       },
                       opts);
    }
    for (std::size_t i = 0; i < 500; ++i) CHECK_EQ(hits[i].load(), 1);
}

TEST("hybrid_for: recalib=EVERY_N N=1000 5 calls") {
    hs::reset_loop_state();
    for (int rep = 0; rep < 5; ++rep) {
        check_exactly_once(1000, hs::Policy::EDP, 1, hs::Recalibrate::EVERY_N);
    }
}

// ── Serial fallback ───────────────────────────────────────────────────────────

TEST("hybrid_for: very large min_parallel_seconds forces serial") {
    std::vector<int> hits(100, 0);
    hs::Options opts;
    opts.min_parallel_seconds = 1e9; // always serial
    hs::reset_loop_state();
    hs::hybrid_for("test.serial", 0, 100,
                   [&](std::size_t i) { hits[i]++; }, opts);
    for (int i = 0; i < 100; ++i) CHECK_EQ(hits[i], 1);
}

// ── begin offset ─────────────────────────────────────────────────────────────

TEST("hybrid_for: non-zero begin offset N=100") {
    std::vector<std::atomic<int>> hits(200);
    for (auto& h : hits) h.store(0, std::memory_order_relaxed);

    hs::reset_loop_state();
    hs::Options opts;
    opts.min_parallel_seconds = 0.0;
    hs::hybrid_for("test.offset", 50, 150,
                   [&](std::size_t i) {
                       hits[i].fetch_add(1, std::memory_order_relaxed);
                   },
                   opts);

    for (std::size_t i = 0; i < 50; ++i) CHECK_EQ(hits[i].load(), 0);
    for (std::size_t i = 50; i < 150; ++i) CHECK_EQ(hits[i].load(), 1);
    for (std::size_t i = 150; i < 200; ++i) CHECK_EQ(hits[i].load(), 0);
}

// ── hybrid_for_range ──────────────────────────────────────────────────────────

TEST("hybrid_for_range: slot index in [0, K)") {
    const int K = hs::runtime_info().n_slots;
    std::vector<std::atomic<int>> slot_seen(static_cast<std::size_t>(std::max(K, 1)));
    for (auto& s : slot_seen) s.store(0, std::memory_order_relaxed);

    hs::Options opts;
    opts.min_parallel_seconds = 0.0;
    hs::reset_loop_state();
    hs::hybrid_for_range("test.range", 0, 10000,
                          [&](std::size_t /*lo*/, std::size_t /*hi*/, int slot) {
                              CHECK(slot >= 0 && slot < K);
                              if (slot >= 0 && slot < K)
                                  slot_seen[static_cast<std::size_t>(slot)].fetch_add(
                                      1, std::memory_order_relaxed);
                          },
                          opts);
    // At least one slot was used
    bool any_used = false;
    for (auto& s : slot_seen)
        if (s.load() > 0) any_used = true;
    CHECK(any_used);
}

// ── Exception rethrow ─────────────────────────────────────────────────────────

TEST("hybrid_for: exception in body is rethrown") {
    hs::reset_loop_state();
    bool caught = false;
    try {
        hs::hybrid_for("test.except", 0, 10000,
                       [](std::size_t i) {
                           if (i == 500) throw std::runtime_error("test exception");
                       });
    } catch (const std::runtime_error& e) {
        caught = true;
        CHECK(std::string(e.what()) == "test exception");
    }
    CHECK(caught);
}

// ── hybrid_reduce ─────────────────────────────────────────────────────────────

TEST("hybrid_reduce: sum 0..999") {
    hs::reset_loop_state();
    hs::Options opts;
    opts.min_parallel_seconds = 0.0;
    const long result = hs::hybrid_reduce<long>(
        "test.reduce", 0, 1000,
        0L,
        [](std::size_t i, long& acc) { acc += static_cast<long>(i); },
        [](long a, long b) { return a + b; },
        opts);
    CHECK_EQ(result, 999L * 1000L / 2L);
}

// ── LoopStats output ──────────────────────────────────────────────────────────

TEST("hybrid_for: LoopStats filled correctly") {
    hs::LoopStats st;
    hs::Options opts;
    opts.stats_out = &st;
    opts.min_parallel_seconds = 0.0;
    hs::reset_loop_state();
    hs::hybrid_for("test.stats", 0, 10000,
                   [](std::size_t) {}, opts);
    CHECK_EQ(st.n_iters, std::size_t{10000});
    CHECK(st.total_seconds >= 0.0);
    CHECK(st.slots_used >= 1);
}

// ── Nested parallel region falls back to serial ───────────────────────────────

TEST("hybrid_for: inside parallel region runs serially") {
    std::vector<std::atomic<int>> hits(100);
    for (auto& h : hits) h.store(0, std::memory_order_relaxed);

    hs::Options opts;
    opts.min_parallel_seconds = 0.0;

#pragma omp parallel num_threads(2)
    {
#pragma omp single
        {
            hs::reset_loop_state();
            hs::hybrid_for("test.nested", 0, 100,
                           [&](std::size_t i) {
                               hits[i].fetch_add(1, std::memory_order_relaxed);
                           },
                           opts);
        }
    }
    for (std::size_t i = 0; i < 100; ++i) CHECK_EQ(hits[i].load(), 1);
}
