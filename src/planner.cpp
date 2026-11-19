/// @file planner.cpp

#include "hybrid_sched/planner.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numeric>

namespace hs {

// ── enumerate_candidates ──────────────────────────────────────────────────────

std::vector<CandidatePlan> enumerate_candidates(const PlannerInput& in) {
    assert(in.slot_speeds != nullptr);
    assert(in.power_model != nullptr);

    const int nP = in.n_p_slots;
    const int nE = in.n_e_slots;
    const auto& speeds = *in.slot_speeds;

    // Precompute prefix sums of speeds (P-slots first, then E-slots)
    // P-slots: indices 0..nP-1, E-slots: indices nP..nP+nE-1
    std::vector<double> p_prefix(static_cast<std::size_t>(nP + 1), 0.0);
    for (int i = 0; i < nP; ++i)
        p_prefix[static_cast<std::size_t>(i + 1)] =
            p_prefix[static_cast<std::size_t>(i)] +
            (static_cast<std::size_t>(i) < speeds.size() ? speeds[static_cast<std::size_t>(i)] : 0.0);

    std::vector<double> e_prefix(static_cast<std::size_t>(nE + 1), 0.0);
    for (int j = 0; j < nE; ++j) {
        const std::size_t idx = static_cast<std::size_t>(nP + j);
        e_prefix[static_cast<std::size_t>(j + 1)] =
            e_prefix[static_cast<std::size_t>(j)] +
            (idx < speeds.size() ? speeds[idx] : 0.0);
    }

    std::vector<CandidatePlan> candidates;
    candidates.reserve(static_cast<std::size_t>((nP + 1) * (nE + 1)));

    constexpr double kEpsilon = 1e-12;

    for (int a = 0; a <= nP; ++a) {
        for (int b = 0; b <= nE; ++b) {
            if (a + b == 0) continue; // must have at least one slot

            const double S = p_prefix[static_cast<std::size_t>(a)] +
                             e_prefix[static_cast<std::size_t>(b)];

            if (S <= kEpsilon) continue;

            // Fork/join overhead
            double fj = 0.0;
            if (in.fork_join_by_threads) {
                const int total_threads = a + b;
                fj = in.fork_join_by_threads[static_cast<std::size_t>(
                    std::min(total_threads, in.n_p_slots + in.n_e_slots))];
            }

            const double T = static_cast<double>(in.n_remaining) / S + fj;
            const double P = in.kappa * in.power_model->predict_w(a, b, in.n_p_phys);
            const double E = P * T;
            const double EDP = E * T;

            CandidatePlan cp;
            cp.a = a;
            cp.b = b;
            cp.throughput = S;
            cp.pred_time_s = T;
            cp.pred_power_w = P;
            cp.pred_energy_j = E;
            cp.pred_edp = EDP;

            // Weights proportional to speed
            cp.weights.resize(static_cast<std::size_t>(a + b));
            for (int i = 0; i < a; ++i)
                cp.weights[static_cast<std::size_t>(i)] =
                    (static_cast<std::size_t>(i) < speeds.size() ? speeds[static_cast<std::size_t>(i)]
                                                                  : 0.0) /
                    S;
            for (int j = 0; j < b; ++j) {
                const std::size_t si = static_cast<std::size_t>(nP + j);
                cp.weights[static_cast<std::size_t>(a + j)] =
                    (si < speeds.size() ? speeds[si] : 0.0) / S;
            }

            candidates.push_back(std::move(cp));
        }
    }

    return candidates;
}

// ── choose_plan ───────────────────────────────────────────────────────────────

Plan choose_plan(const PlannerInput& in) {
    assert(in.slot_speeds != nullptr);
    assert(in.power_model != nullptr);

    const int nP = in.n_p_slots;
    const int nE = in.n_e_slots;
    const auto& speeds = *in.slot_speeds;

    constexpr double kEpsilon = 1e-12;

    Plan result;

    // Handle forced policies
    if (in.policy == Policy::TIME_BALANCED) {
        // All slots, weights by speed
        const double S = std::accumulate(speeds.begin(), speeds.end(), 0.0);
        if (S <= kEpsilon) {
            result.run_serial = true;
            result.serial_slot = 0;
            return result;
        }
        result.a = nP;
        result.b = nE;
        result.weights.resize(static_cast<std::size_t>(nP + nE));
        for (std::size_t i = 0; i < speeds.size(); ++i)
            result.weights[i] = speeds[i] / S;

        double fj = 0.0;
        if (in.fork_join_by_threads) {
            fj = in.fork_join_by_threads[static_cast<std::size_t>(nP + nE)];
        }
        result.pred_time_s = static_cast<double>(in.n_remaining) / S + fj;
        result.pred_power_w =
            in.kappa * in.power_model->predict_w(nP, nE, in.n_p_phys);
        result.pred_energy_j = result.pred_power_w * result.pred_time_s;
        result.pred_edp = result.pred_energy_j * result.pred_time_s;

        if (result.pred_time_s < in.min_parallel_seconds) {
            result.run_serial = true;
            result.serial_slot = 0;
        }
        return result;
    }

    if (in.policy == Policy::P_ONLY) {
        // Only P slots
        if (nP == 0) {
            // Fall back to all E
            return [&]() {
                PlannerInput tmp = in;
                tmp.policy = Policy::E_ONLY;
                return choose_plan(tmp);
            }();
        }
        double S = 0.0;
        for (int i = 0; i < nP; ++i) {
            if (static_cast<std::size_t>(i) < speeds.size()) S += speeds[static_cast<std::size_t>(i)];
        }
        if (S <= kEpsilon) {
            result.run_serial = true;
            result.serial_slot = 0;
            return result;
        }
        result.a = nP;
        result.b = 0;
        result.weights.resize(static_cast<std::size_t>(nP));
        for (int i = 0; i < nP; ++i)
            result.weights[static_cast<std::size_t>(i)] =
                (static_cast<std::size_t>(i) < speeds.size() ? speeds[static_cast<std::size_t>(i)]
                                                              : 0.0) /
                S;

        double fj = 0.0;
        if (in.fork_join_by_threads)
            fj = in.fork_join_by_threads[static_cast<std::size_t>(nP)];

        result.pred_time_s = static_cast<double>(in.n_remaining) / S + fj;
        result.pred_power_w = in.kappa * in.power_model->predict_w(nP, 0, in.n_p_phys);
        result.pred_energy_j = result.pred_power_w * result.pred_time_s;
        result.pred_edp = result.pred_energy_j * result.pred_time_s;

        if (result.pred_time_s < in.min_parallel_seconds) {
            result.run_serial = true;
            result.serial_slot = 0;
        }
        return result;
    }

    if (in.policy == Policy::E_ONLY) {
        if (nE == 0) {
            return [&]() {
                PlannerInput tmp = in;
                tmp.policy = Policy::P_ONLY;
                return choose_plan(tmp);
            }();
        }
        double S = 0.0;
        for (int j = 0; j < nE; ++j) {
            const std::size_t idx = static_cast<std::size_t>(nP + j);
            if (idx < speeds.size()) S += speeds[idx];
        }
        if (S <= kEpsilon) {
            result.run_serial = true;
            result.serial_slot = 0;
            return result;
        }
        result.a = 0;
        result.b = nE;
        result.weights.resize(static_cast<std::size_t>(nE));
        for (int j = 0; j < nE; ++j) {
            const std::size_t idx = static_cast<std::size_t>(nP + j);
            result.weights[static_cast<std::size_t>(j)] =
                (idx < speeds.size() ? speeds[idx] : 0.0) / S;
        }

        double fj = 0.0;
        if (in.fork_join_by_threads)
            fj = in.fork_join_by_threads[static_cast<std::size_t>(nE)];

        result.pred_time_s = static_cast<double>(in.n_remaining) / S + fj;
        result.pred_power_w = in.kappa * in.power_model->predict_w(0, nE, in.n_p_phys);
        result.pred_energy_j = result.pred_power_w * result.pred_time_s;
        result.pred_edp = result.pred_energy_j * result.pred_time_s;

        if (result.pred_time_s < in.min_parallel_seconds) {
            result.run_serial = true;
            result.serial_slot = 0;
        }
        return result;
    }

    if (in.policy == Policy::FIXED_RATIO) {
        // All slots, weights: P=ratio, E=1
        const double ratio = in.fixed_ratio_p_over_e > 0.0 ? in.fixed_ratio_p_over_e : 1.0;
        result.a = nP;
        result.b = nE;
        const int total = nP + nE;
        result.weights.resize(static_cast<std::size_t>(total));
        double wsum = 0.0;
        for (int i = 0; i < nP; ++i) wsum += ratio;
        for (int j = 0; j < nE; ++j) wsum += 1.0;
        if (wsum <= kEpsilon) wsum = 1.0;
        for (int i = 0; i < nP; ++i) result.weights[static_cast<std::size_t>(i)] = ratio / wsum;
        for (int j = 0; j < nE; ++j)
            result.weights[static_cast<std::size_t>(nP + j)] = 1.0 / wsum;

        // Predict using measured speeds if available
        double S = std::accumulate(speeds.begin(), speeds.end(), 0.0);
        if (S <= kEpsilon) S = 1.0;

        double fj = 0.0;
        if (in.fork_join_by_threads)
            fj = in.fork_join_by_threads[static_cast<std::size_t>(total)];

        result.pred_time_s = static_cast<double>(in.n_remaining) / S + fj;
        result.pred_power_w =
            in.kappa * in.power_model->predict_w(nP, nE, in.n_p_phys);
        result.pred_energy_j = result.pred_power_w * result.pred_time_s;
        result.pred_edp = result.pred_energy_j * result.pred_time_s;

        if (result.pred_time_s < in.min_parallel_seconds) {
            result.run_serial = true;
            result.serial_slot = 0;
        }
        return result;
    }

    // ── Policy::EDP or Policy::ENERGY: enumerate all candidates ──────────────
    const auto candidates = enumerate_candidates(in);
    if (candidates.empty()) {
        result.run_serial = true;
        result.serial_slot = 0;
        return result;
    }

    // Find the best candidate by objective
    const CandidatePlan* best = nullptr;
    double best_obj = std::numeric_limits<double>::max();

    for (const auto& cp : candidates) {
        const double obj = (in.policy == Policy::EDP) ? cp.pred_edp : cp.pred_energy_j;
        if (obj < best_obj) {
            best_obj = obj;
            best = &cp;
        } else if (std::abs(obj - best_obj) / (best_obj + kEpsilon) < 0.01) {
            // Tie-break (within 1%): fewer threads, then fewer E threads
            const int best_total = best->a + best->b;
            const int cp_total = cp.a + cp.b;
            if (cp_total < best_total ||
                (cp_total == best_total && cp.b < best->b)) {
                best_obj = obj;
                best = &cp;
            }
        }
    }

    if (!best) {
        result.run_serial = true;
        result.serial_slot = 0;
        return result;
    }

    result.a = best->a;
    result.b = best->b;
    result.weights = best->weights;
    result.pred_time_s = best->pred_time_s;
    result.pred_power_w = best->pred_power_w;
    result.pred_energy_j = best->pred_energy_j;
    result.pred_edp = best->pred_edp;

    if (result.pred_time_s < in.min_parallel_seconds) {
        result.run_serial = true;
        // Run on the fastest slot
        double max_speed = -1.0;
        for (std::size_t i = 0; i < speeds.size(); ++i) {
            if (speeds[i] > max_speed) {
                max_speed = speeds[i];
                result.serial_slot = static_cast<int>(i);
            }
        }
    }

    return result;
}

} // namespace hs
