/// @file partition.cpp

#include "hybrid_sched/partition.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace hs {

// ── compute_static_blocks ─────────────────────────────────────────────────────

std::vector<SlotBlock> compute_static_blocks(std::size_t global_begin,
                                              std::size_t n_static,
                                              const std::vector<double>& weights,
                                              std::size_t grain) {
    assert(grain >= 1);
    const std::size_t n_slots = weights.size();

    std::vector<SlotBlock> blocks(n_slots, {global_begin, global_begin});
    if (n_slots == 0 || n_static == 0) return blocks;

    // Normalise weights
    const double w_sum = std::accumulate(weights.begin(), weights.end(), 0.0);
    if (w_sum <= 0.0) {
        // Equal weights fallback
        std::vector<double> eq(n_slots, 1.0 / static_cast<double>(n_slots));
        return compute_static_blocks(global_begin, n_static, eq, grain);
    }

    // Number of grains to distribute
    const std::size_t n_grains = n_static / grain; // floor
    // Any remainder after grain alignment
    const std::size_t leftover = n_static - n_grains * grain;

    // Largest-remainder method on grains
    std::vector<double> real_grains(n_slots);
    for (std::size_t i = 0; i < n_slots; ++i)
        real_grains[i] = weights[i] / w_sum * static_cast<double>(n_grains);

    std::vector<std::size_t> floor_grains(n_slots);
    double frac_sum = 0.0;
    std::size_t floor_sum = 0;
    for (std::size_t i = 0; i < n_slots; ++i) {
        floor_grains[i] = static_cast<std::size_t>(real_grains[i]);
        floor_sum += floor_grains[i];
    }

    // Distribute remainder grains by largest fractional part
    const std::size_t remainder_grains = n_grains - floor_sum;
    if (remainder_grains > 0) {
        // Sort by fractional part descending
        std::vector<std::size_t> order(n_slots);
        std::iota(order.begin(), order.end(), 0);
        std::vector<double> fracs(n_slots);
        for (std::size_t i = 0; i < n_slots; ++i)
            fracs[i] = real_grains[i] - static_cast<double>(floor_grains[i]);
        std::sort(order.begin(), order.end(),
                  [&fracs](std::size_t a, std::size_t b) { return fracs[a] > fracs[b]; });
        for (std::size_t r = 0; r < remainder_grains; ++r)
            floor_grains[order[r]] += 1;
    }
    (void)frac_sum;

    // Build blocks: contiguous, each size = floor_grains[i] * grain
    std::size_t cur = global_begin;
    for (std::size_t i = 0; i < n_slots; ++i) {
        blocks[i].begin = cur;
        blocks[i].end = cur + floor_grains[i] * grain;
        cur = blocks[i].end;
    }

    // The leftover iterations (< grain) go to the last slot
    if (leftover > 0 && n_slots > 0) {
        blocks[n_slots - 1].end += leftover;
        cur += leftover;
    }

    // Sanity: cur must equal global_begin + n_static
    assert(cur == global_begin + n_static);

    return blocks;
}

// ── DynamicTail ───────────────────────────────────────────────────────────────

void DynamicTail::init(std::size_t begin, std::size_t end,
                        std::size_t chunk, std::size_t grain) noexcept {
    tail_begin = begin;
    tail_end = end;
    chunk_size = std::max(std::max(chunk, grain), std::size_t{1});
    counter.store(begin, std::memory_order_relaxed);
}

std::pair<std::size_t, std::size_t> DynamicTail::next_chunk() noexcept {
    const std::size_t lo = counter.fetch_add(chunk_size, std::memory_order_relaxed);
    if (lo >= tail_end) return {tail_end, tail_end}; // exhausted
    const std::size_t hi = std::min(lo + chunk_size, tail_end);
    return {lo, hi};
}

} // namespace hs
