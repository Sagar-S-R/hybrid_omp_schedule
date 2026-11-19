/// @file test_partition.cpp
/// Tests for the partitioner: coverage, no overlap, contiguity, weight sum.

#include "minitest.hpp"
#include "hybrid_sched/partition.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

// Verify the union of all blocks = [global_begin, global_begin + n_static)
static void verify_partition(std::size_t global_begin, std::size_t n_static,
                               const std::vector<hs::SlotBlock>& blocks) {
    std::size_t covered = 0;
    std::size_t prev_end = global_begin;
    for (const auto& b : blocks) {
        // Contiguity
        CHECK_EQ(b.begin, prev_end);
        CHECK(b.end >= b.begin);
        covered += (b.end - b.begin);
        prev_end = b.end;
    }
    CHECK_EQ(covered, n_static);
    CHECK_EQ(prev_end, global_begin + n_static);
}

TEST("partition: equal weights, grain=1") {
    const std::vector<double> w{1.0, 1.0, 1.0, 1.0};
    const auto blocks = hs::compute_static_blocks(0, 100, w, 1);
    verify_partition(0, 100, blocks);
    // Each should get exactly 25
    for (const auto& b : blocks) CHECK_EQ(b.end - b.begin, std::size_t{25});
}

TEST("partition: equal weights, N not divisible") {
    const std::vector<double> w{1.0, 1.0, 1.0};
    const auto blocks = hs::compute_static_blocks(0, 10, w, 1);
    verify_partition(0, 10, blocks);
}

TEST("partition: grain > 1") {
    const std::vector<double> w{1.0, 1.0, 1.0, 1.0};
    const std::size_t grain = 8;
    const std::size_t n = 96;
    const auto blocks = hs::compute_static_blocks(0, n, w, grain);
    verify_partition(0, n, blocks);
    // First 3 slots: block sizes must be multiples of grain
    for (std::size_t i = 0; i + 1 < blocks.size(); ++i) {
        const std::size_t sz = blocks[i].end - blocks[i].begin;
        CHECK_EQ(sz % grain, std::size_t{0});
    }
}

TEST("partition: non-power-of-two grain, uneven split") {
    const std::vector<double> w{3.0, 1.0}; // 3:1 ratio
    const std::size_t n = 20;
    const std::size_t grain = 4;
    const auto blocks = hs::compute_static_blocks(0, n, w, grain);
    verify_partition(0, n, blocks);
}

TEST("partition: non-zero global_begin") {
    const std::vector<double> w{1.0, 1.0};
    const auto blocks = hs::compute_static_blocks(100, 50, w, 1);
    verify_partition(100, 50, blocks);
    CHECK_EQ(blocks[0].begin, std::size_t{100});
}

TEST("partition: single slot") {
    const std::vector<double> w{1.0};
    const auto blocks = hs::compute_static_blocks(0, 77, w, 1);
    verify_partition(0, 77, blocks);
    CHECK_EQ(blocks[0].begin, std::size_t{0});
    CHECK_EQ(blocks[0].end, std::size_t{77});
}

TEST("partition: n_static=0 gives empty blocks") {
    const std::vector<double> w{1.0, 1.0};
    const auto blocks = hs::compute_static_blocks(0, 0, w, 1);
    CHECK_EQ(blocks.size(), std::size_t{2});
    for (const auto& b : blocks) CHECK_EQ(b.begin, b.end);
}

TEST("partition: random grain stress") {
    // Random-ish sizes via deterministic arithmetic
    for (std::size_t n : {1UL, 2UL, 7UL, 63UL, 64UL, 65UL, 1000UL, 10000UL}) {
        for (std::size_t grain : {1UL, 3UL, 7UL, 16UL}) {
            for (int slots : {1, 2, 3, 4, 8}) {
                std::vector<double> w(static_cast<std::size_t>(slots), 1.0);
                const auto blocks = hs::compute_static_blocks(0, n, w, grain);
                verify_partition(0, n, blocks);
            }
        }
    }
}

TEST("partition: skewed weights") {
    // One slot gets 90%, another 10%
    const std::vector<double> w{9.0, 1.0};
    const std::size_t n = 1000;
    const auto blocks = hs::compute_static_blocks(0, n, w, 1);
    verify_partition(0, n, blocks);
    // Larger slot gets more iterations
    CHECK_GT(blocks[0].end - blocks[0].begin, blocks[1].end - blocks[1].begin);
}

TEST("partition: DynamicTail exhaustion") {
    hs::DynamicTail tail;
    tail.init(100, 200, 10, 1);
    std::size_t total = 0;
    while (true) {
        auto [lo, hi] = tail.next_chunk();
        if (lo >= hi) break;
        CHECK(lo >= 100 && hi <= 200);
        CHECK(hi > lo);
        total += (hi - lo);
    }
    CHECK_EQ(total, std::size_t{100});
}

TEST("partition: DynamicTail empty range") {
    hs::DynamicTail tail;
    tail.init(50, 50, 10, 1); // empty
    auto [lo, hi] = tail.next_chunk();
    CHECK_EQ(lo, hi);
}
