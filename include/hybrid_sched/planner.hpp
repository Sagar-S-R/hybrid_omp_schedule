#pragma once
/// @file planner.hpp
/// @brief Enumerate candidate (a, b) configs, predict EDP/time/energy, pick the best.

#include "power_model.hpp"
#include "topology.hpp"

#include <cstddef>
#include <vector>

namespace hs {

/// Policy for choosing the core configuration.
enum class Policy {
    EDP,           ///< Minimise predicted energy * time  (OUR METHOD, default)
    ENERGY,        ///< Minimise predicted energy
    TIME_BALANCED, ///< Always use all slots, weights = measured speeds
    P_ONLY,        ///< Force: P slots only
    E_ONLY,        ///< Force: E slots only
    FIXED_RATIO    ///< No calibration: fixed P:E weight ratio
};

/// One candidate configuration's predicted metrics.
struct CandidatePlan {
    int a{0};               ///< Number of P slots used
    int b{0};               ///< Number of E slots used
    double throughput{0.0}; ///< Sum of slot speeds (iters/s)
    double pred_time_s{0.0};
    double pred_power_w{0.0};
    double pred_energy_j{0.0};
    double pred_edp{0.0};   ///< energy * time
    std::vector<double> weights; ///< Per-slot fractional iteration weights (size a+b)
};

/// The chosen plan for one loop call.
struct Plan {
    int a{0};
    int b{0};
    std::vector<double> weights; ///< size a+b
    double pred_time_s{0.0};
    double pred_power_w{0.0};
    double pred_energy_j{0.0};
    double pred_edp{0.0};
    bool run_serial{false};        ///< True if predicted time < min_parallel_seconds
    int serial_slot{0};            ///< Which slot to use for serial execution
};

/// Input to the planner.
struct PlannerInput {
    const std::vector<double>* slot_speeds{nullptr}; ///< iters/s per slot (P-slots first)
    int n_p_slots{0};
    int n_e_slots{0};
    int n_p_phys{0};           ///< Physical P cores (for SMT model)
    std::size_t n_remaining{0}; ///< Iterations to plan for
    double kappa{1.0};         ///< Power scale factor (from online refinement)
    double fork_join_s{0.0};   ///< Measured fork/join overhead per thread count (indexed by a+b)
    const double* fork_join_by_threads{nullptr}; ///< [0..n_total_slots] fork/join seconds
    Policy policy{Policy::EDP};
    double fixed_ratio_p_over_e{2.0}; ///< Only for FIXED_RATIO
    double min_parallel_seconds{50e-6};
    const PowerModel* power_model{nullptr};
};

/// Choose the best plan.
[[nodiscard]] Plan choose_plan(const PlannerInput& in);

/// Enumerate all candidate plans (for oracle / analysis).
[[nodiscard]] std::vector<CandidatePlan> enumerate_candidates(const PlannerInput& in);

} // namespace hs
