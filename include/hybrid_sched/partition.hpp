#pragma once
/// @file partition.hpp
/// @brief Weighted-static + dynamic-tail partitioner.
///
/// Correctness invariant: every iteration in [begin, end) is covered exactly once.
/// The static portion tiles [begin, begin+R_static) with blocks proportional to
/// the slot weights using the largest-remainder method.
/// The dynamic tail [begin+R_static, end) is handed out via an atomic counter.

#include <atomic>
#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

namespace hs {

/// Static block assignment for one slot.
struct SlotBlock {
    std::size_t begin;
    std::size_t end;   ///< exclusive; begin==end means empty block
};

/// Compute per-slot static blocks using the largest-remainder method.
/// @param global_begin  Start of the region to partition (inclusive).
/// @param n_static      Number of iterations to partition statically.
/// @param weights       Per-slot weights (must be > 0, need not sum to 1).
/// @param grain         All block sizes are multiples of grain (>= 1).
/// @returns Vector of SlotBlock, same length as weights.
///          The union of all blocks = [global_begin, global_begin + n_static) exactly.
[[nodiscard]] std::vector<SlotBlock> compute_static_blocks(std::size_t global_begin,
                                                            std::size_t n_static,
                                                            const std::vector<double>& weights,
                                                            std::size_t grain);

/// Shared state for the dynamic tail counter.
/// alignas(64) prevents false sharing between slots reading this and their own per-slot data.
struct alignas(64) DynamicTail {
    std::atomic<std::size_t> counter;
    std::size_t tail_begin;
    std::size_t tail_end;
    std::size_t chunk_size;

    DynamicTail() noexcept : counter(0), tail_begin(0), tail_end(0), chunk_size(1) {}

    /// Initialise for a new dynamic tail region.
    void init(std::size_t begin, std::size_t end, std::size_t chunk, std::size_t grain) noexcept;

    /// Claim the next chunk.  Returns {lo, hi} with lo==hi if exhausted.
    [[nodiscard]] std::pair<std::size_t, std::size_t> next_chunk() noexcept;
};

} // namespace hs
