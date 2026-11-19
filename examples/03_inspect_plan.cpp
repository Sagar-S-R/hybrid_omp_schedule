/// @file 03_inspect_plan.cpp
/// Prints topology, RAPL availability, and chosen plan diagnostics per call.

#include "hybrid_sched/hybrid_for.hpp"

#include <cstddef>
#include <iostream>
#include <vector>

int main() {
    // ── Runtime info ──────────────────────────────────────────────────────────
    const hs::RuntimeInfo ri = hs::runtime_info();
    std::cout << "=== Runtime Info ===\n";
    std::cout << ri.topology_description << "\n";
    std::cout << "RAPL available:        " << (ri.rapl_available ? "yes" : "no") << "\n";
    std::cout << "Power profile loaded:  " << (ri.power_profile_loaded ? "yes" : "no") << "\n\n";

    // ── Loop with diagnostics ─────────────────────────────────────────────────
    constexpr std::size_t N = 1 << 20;
    std::vector<double> data(N, 1.0);

    hs::LoopStats stats;
    hs::Options opts;
    opts.stats_out = &stats;
    opts.policy    = hs::Policy::EDP;
    opts.recalibrate = hs::Recalibrate::EVERY_CALL;

    // Run 3 times to show per-call plan evolution
    for (int call = 0; call < 3; ++call) {
        hs::hybrid_for("inspect.loop", 0, N,
                       [&](std::size_t i) { data[i] *= 1.000001; },
                       opts);

        std::cout << "=== Call " << call + 1 << " ===\n";
        std::cout << "  Calibrated this call: " << (stats.calibrated_this_call ? "yes" : "no") << "\n";
        std::cout << "  P threads used:       " << stats.p_threads << "\n";
        std::cout << "  E threads used:       " << stats.e_threads << "\n";
        std::cout << "  Warmup time:          " << stats.warmup_seconds * 1e3 << " ms\n";
        std::cout << "  Plan time:            " << stats.plan_seconds  * 1e6 << " us\n";
        std::cout << "  Main time:            " << stats.main_seconds  * 1e3 << " ms\n";
        std::cout << "  Pred time:            " << stats.pred_time_s   * 1e3 << " ms\n";
        std::cout << "  Imbalance:            " << stats.imbalance_percent << " %\n";
        std::cout << "  Pred power:           " << stats.pred_power_w << " W\n";
        if (!std::isnan(stats.meas_energy_j))
            std::cout << "  Meas energy:          " << stats.meas_energy_j << " J\n";
        else
            std::cout << "  Meas energy:          NaN (RAPL unavailable or window too short)\n";
        std::cout << "\n";
    }

    return 0;
}
