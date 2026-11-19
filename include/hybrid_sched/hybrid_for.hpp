#pragma once
/// @file hybrid_for.hpp
/// @brief Public API: hs::hybrid_for, hs::hybrid_for_range, hs::hybrid_reduce, Options, Policy.
///
/// Usage — replace:
///   #pragma omp parallel for
///   for (size_t i = 0; i < n; ++i) body(i);
/// with:
///   hs::hybrid_for("my.loop.name", 0, n, [&](std::size_t i) { body(i); });

#include "function_ref.hpp"
#include "loop_state.hpp"
#include "planner.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace hs {

// Re-export enums that are used in Options for convenience
using Policy = ::hs::Policy;
using Recalibrate = ::hs::Recalibrate;

/// Per-call diagnostic output (filled when Options::stats_out != nullptr).
struct LoopStats {
    std::size_t n_iters{0};
    int slots_used{0};
    int p_threads{0};
    int e_threads{0};

    double warmup_seconds{0.0};
    double plan_seconds{0.0};
    double main_seconds{0.0};
    double total_seconds{0.0};

    double pred_time_s{0.0};
    double pred_power_w{0.0};
    double pred_energy_j{0.0};
    double pred_edp{0.0};

    double meas_energy_j{std::numeric_limits<double>::quiet_NaN()};
    double imbalance_percent{0.0};

    std::vector<double> slot_speed_iters_per_s;
    std::vector<double> slot_finish_s;

    bool calibrated_this_call{false};
};

/// Options controlling one invocation of hybrid_for / hybrid_for_range / hybrid_reduce.
struct Options {
    Policy      policy               = Policy::EDP;
    Recalibrate recalibrate          = Recalibrate::EVERY_CALL;
    int         recalibrate_every_n  = 8;
    double      drift_threshold      = 0.15;
    double      warmup_fraction      = 0.03;
    std::size_t min_warmup_iters     = 0;          ///< 0 = auto (>= 8 per slot, <= 20% of loop)
    double      tail_fraction        = 0.10;
    std::size_t grain                = 1;
    bool        use_smt              = true;
    double      fixed_ratio_p_over_e = 2.0;
    double      min_parallel_seconds = 50e-6;
    LoopStats*  stats_out            = nullptr;    ///< Optional; filled each call if non-null
};

/// Summary of the runtime (topology, RAPL, power model status).
struct RuntimeInfo {
    int n_p_slots{0};
    int n_e_slots{0};
    int n_p_phys{0};
    bool smt_active{false};
    bool hybrid_cpu{false};
    bool rapl_available{false};
    bool power_profile_loaded{false};
    int n_slots{0};
    std::string topology_description;
};

// ── Non-template declarations (must precede template definitions) ─────────────

/// Internal implementation called by the template wrappers.
void hybrid_for_range_impl(const char* loop_name, std::size_t begin, std::size_t end,
                             FunctionRef<void(std::size_t, std::size_t, int)> body,
                             const Options& opts);

/// Get a summary of the current runtime state.
[[nodiscard]] RuntimeInfo runtime_info();

/// Set default options for all subsequent calls.
void set_default_options(const Options& opts);

/// Forget all per-loop persistent state (useful between tests / experiments).
void reset_loop_state();

/// Optional cleanup (called automatically at program exit if not called manually).
void shutdown();

// ── Core entry points (templates) ───────────────────────────────────────────

/// Core entry: body(lo, hi, slot) processes iterations [lo, hi).
/// `slot` is the 0-based worker slot index (stable within a call).
template <class RangeBody>
void hybrid_for_range(const char* loop_name, std::size_t begin, std::size_t end,
                       RangeBody&& body, const Options& opts = {}) {
    FunctionRef<void(std::size_t, std::size_t, int)> ref{body};
    hybrid_for_range_impl(loop_name, begin, end, ref, opts);
}

/// Convenience: per-iteration body(i).
template <class Body>
void hybrid_for(const char* loop_name, std::size_t begin, std::size_t end,
                Body&& body, const Options& opts = {}) {
    hybrid_for_range(loop_name, begin, end,
                     [&body](std::size_t lo, std::size_t hi, int /*slot*/) {
                         for (std::size_t i = lo; i < hi; ++i) body(i);
                     },
                     opts);
}

/// Reduction: body(i, acc) accumulates into acc; combine merges accumulators in slot order.
template <class T, class Body, class Combine>
T hybrid_reduce(const char* loop_name, std::size_t begin, std::size_t end,
                T identity, Body&& body, Combine&& combine, const Options& opts = {}) {
    // Allocate per-slot accumulators
    RuntimeInfo ri = runtime_info();
    const int max_slots = ri.n_slots > 0 ? ri.n_slots : 1;
    std::vector<T> accs(static_cast<std::size_t>(max_slots), identity);

    hybrid_for_range(loop_name, begin, end,
                     [&accs, &body](std::size_t lo, std::size_t hi, int slot) {
                         auto& acc = accs[static_cast<std::size_t>(slot)];
                         for (std::size_t i = lo; i < hi; ++i) body(i, acc);
                     },
                     opts);

    T result = identity;
    for (auto& a : accs) result = combine(result, a);
    return result;
}

} // namespace hs
