#pragma once
/// @file calibrator.hpp
/// @brief Warm-up measurement: runs a small fraction of iterations on all slots
///        and returns per-slot iteration speeds (iters/second).

#include "function_ref.hpp"
#include "topology.hpp"
#include "timer.hpp"

#include <atomic>
#include <cstddef>
#include <vector>

namespace hs {

/// Per-slot result from one calibration run.
struct SlotSpeed {
    double iters_per_s{0.0};   ///< Measured speed (>0 always after calibration)
    std::size_t iters_done{0}; ///< Actual iterations executed by this slot
    double busy_s{0.0};        ///< Time spent in work chunks
    bool got_work{true};       ///< False if the slot obtained zero chunks
};

/// Parameters for one calibration run.
struct CalibrateParams {
    std::size_t begin{0};
    std::size_t end{0};           ///< end of warmup slice: [begin, begin+W)
    std::size_t grain{1};
    const std::vector<int>* slot_cpus{nullptr};  ///< CPU ids for each slot (size = n_slots)
    bool disable_pinning{false};
};

/// Run the warm-up measurement.
/// @param body   Function called as body(lo, hi, slot) for range [lo, hi).
/// @param params Parameters controlling the warm-up.
/// @return Per-slot speeds; speeds are > 0 (zero-work slots get 5% of median).
[[nodiscard]] std::vector<SlotSpeed> calibrate(FunctionRef<void(std::size_t, std::size_t, int)> body,
                                                const CalibrateParams& params);

} // namespace hs
