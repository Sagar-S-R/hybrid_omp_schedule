/// @file calibrator.cpp

#include "hybrid_sched/calibrator.hpp"
#include "hybrid_sched/timer.hpp"
#include "hybrid_sched/topology.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#include <omp.h>

namespace hs {

std::vector<SlotSpeed> calibrate(FunctionRef<void(std::size_t, std::size_t, int)> body,
                                  const CalibrateParams& params) {
    const std::size_t begin = params.begin;
    const std::size_t end = params.end;
    const std::size_t grain = std::max<std::size_t>(1, params.grain);
    const int n_slots = params.slot_cpus ? static_cast<int>(params.slot_cpus->size()) : 1;

    const std::size_t total = (end > begin) ? (end - begin) : 0;

    std::vector<SlotSpeed> results(static_cast<std::size_t>(n_slots));
    if (total == 0 || n_slots == 0) return results;

    // Chunk size: W / (K * 16), at least grain
    const std::size_t chunk = std::max(grain, total / static_cast<std::size_t>(n_slots * 16));

    std::atomic<std::size_t> counter{begin};

    omp_set_dynamic(0);

#pragma omp parallel num_threads(n_slots)
    {
        const int slot = omp_get_thread_num();
        if (slot < n_slots) {
            // Pin to slot CPU
            if (!params.disable_pinning && params.slot_cpus) {
                pin_current_thread((*params.slot_cpus)[static_cast<std::size_t>(slot)]);
            }

            std::size_t iters_done = 0;
            double busy_s = 0.0;
            bool got_work = false;

            while (true) {
                const std::size_t lo = counter.fetch_add(chunk, std::memory_order_relaxed);
                if (lo >= end) break;
                const std::size_t hi = std::min(lo + chunk, end);

                // Snap time around this chunk only
                const double t0 = now_s();
                body(lo, hi, slot);
                const double t1 = now_s();

                iters_done += (hi - lo);
                busy_s += (t1 - t0);
                got_work = true;
            }

            SlotSpeed& ss = results[static_cast<std::size_t>(slot)];
            ss.iters_done = iters_done;
            ss.busy_s = busy_s;
            ss.got_work = got_work;

            constexpr double kEpsilon = 1e-12;
            if (busy_s > kEpsilon && iters_done > 0) {
                ss.iters_per_s = static_cast<double>(iters_done) / busy_s;
            } else {
                ss.iters_per_s = 0.0; // will be patched below
            }
        }
    }

    // Compute median of non-zero speeds for imputation
    std::vector<double> nonzero;
    nonzero.reserve(static_cast<std::size_t>(n_slots));
    for (const auto& s : results) {
        if (s.iters_per_s > 0.0) nonzero.push_back(s.iters_per_s);
    }

    double median_speed = 1.0; // fallback
    if (!nonzero.empty()) {
        std::sort(nonzero.begin(), nonzero.end());
        const std::size_t mid = nonzero.size() / 2;
        median_speed = (nonzero.size() % 2 == 1)
                           ? nonzero[mid]
                           : (nonzero[mid - 1] + nonzero[mid]) * 0.5;
    }

    // Impute zero-speed slots with 5% of median (as per spec)
    for (auto& ss : results) {
        if (ss.iters_per_s <= 0.0) {
            ss.iters_per_s = 0.05 * median_speed;
            ss.got_work = false;
            util::log("Calibrator: slot got no work, imputing speed = 0.05 * median");
        }
    }

    return results;
}

} // namespace hs
