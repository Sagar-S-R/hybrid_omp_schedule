/// @file hybrid_for.cpp
/// Runtime singleton + orchestration of one hybrid_for_range call.

#include "hybrid_sched/hybrid_for.hpp"

#include "hybrid_sched/calibrator.hpp"
#include "hybrid_sched/energy.hpp"
#include "hybrid_sched/function_ref.hpp"
#include "hybrid_sched/loop_state.hpp"
#include "hybrid_sched/partition.hpp"
#include "hybrid_sched/planner.hpp"
#include "hybrid_sched/power_model.hpp"
#include "hybrid_sched/timer.hpp"
#include "hybrid_sched/topology.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <omp.h>

#if defined(__linux__)
#include <sched.h>
#endif

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// Runtime singleton
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Measured fork/join overhead for each thread count [0..max_slots].
std::vector<double> g_fork_join_overhead_s;

/// Measure empty parallel region overhead for each thread count 1..n.
void measure_fork_join_overhead(int max_threads) {
    g_fork_join_overhead_s.assign(static_cast<std::size_t>(max_threads + 1), 0.0);
    const int reps = 5;
    for (int t = 1; t <= max_threads; ++t) {
        double total = 0.0;
        for (int r = 0; r < reps; ++r) {
            const double t0 = now_s();
#pragma omp parallel num_threads(t)
            {
                // empty
            }
            total += now_s() - t0;
        }
        g_fork_join_overhead_s[static_cast<std::size_t>(t)] = total / reps;
    }
}

struct Runtime {
    Topology topo;
    PowerModel power_model;
    EnergyReader energy;
    LoopStateRegistry loop_registry;
    Options default_opts;

    // All CPU ids in slot order: P primaries, P SMT siblings, then E
    std::vector<int> all_slot_cpus; // size = n_p_slots + n_e_slots

    bool initialised{false};

    void init() {
        omp_set_dynamic(0);
        omp_set_max_active_levels(1);

        topo = detect_topology();
        all_slot_cpus.clear();
        for (int c : topo.p_cpus) all_slot_cpus.push_back(c);
        for (int c : topo.e_cpus) all_slot_cpus.push_back(c);

        // Log topology
        if (util::verbose()) {
            util::log("Topology: " + describe_topology(topo));
            util::log("RAPL available: " + std::string(energy.available() ? "yes" : "no"));
            util::log("Power profile loaded: " +
                      std::string(power_model.profile_loaded() ? "yes" : "no"));
        }

        // Measure fork/join overhead
        const int max_t = static_cast<int>(all_slot_cpus.size());
        if (max_t > 0) measure_fork_join_overhead(max_t);

        initialised = true;
    }
};

Runtime& get_runtime() {
    static Runtime rt;
    static std::once_flag flag;
    std::call_once(flag, [&]() { rt.init(); });
    return rt;
}

// CSV trace file
std::mutex g_trace_mutex;
std::string g_trace_path;
bool g_trace_header_written = false;

void trace_init() {
    g_trace_path = util::getenv_str("HS_TRACE_CSV");
}

void trace_row(const std::string& loop_name, std::size_t call_idx, bool calibrated,
               std::size_t N, int a, int b,
               double pred_t, double pred_e, double pred_edp,
               double meas_t, double meas_e, double imbalance) {
    if (g_trace_path.empty()) return;
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    std::ofstream f(g_trace_path, std::ios::app);
    if (!f.is_open()) return;
    if (!g_trace_header_written) {
        f << "loop_name,call_idx,calibrated,N,a,b,"
             "pred_time_s,pred_energy_j,pred_edp,"
             "meas_time_s,meas_energy_j,imbalance_pct\n";
        g_trace_header_written = true;
    }
    f << loop_name << ',' << call_idx << ',' << (calibrated ? 1 : 0) << ','
      << N << ',' << a << ',' << b << ','
      << pred_t << ',' << pred_e << ',' << pred_edp << ','
      << meas_t << ',' << meas_e << ',' << imbalance << '\n';
}

// Per-slot padded timing data (avoid false sharing)
struct alignas(64) SlotFinish {
    double finish_s{0.0};
    double static_done_s{0.0};
    double _pad[6];
};

// RAII guard to save and restore the calling thread's CPU affinity.
// Pinned worker threads leave their affinity restricted after the
// parallel region; this restores the caller's original mask on exit.
#if defined(__linux__)
struct AffinityGuard {
    cpu_set_t saved_mask;
    bool valid{false};
    AffinityGuard() {
        CPU_ZERO(&saved_mask);
        if (sched_getaffinity(0, sizeof(saved_mask), &saved_mask) == 0)
            valid = true;
    }
    ~AffinityGuard() {
        if (valid)
            sched_setaffinity(0, sizeof(saved_mask), &saved_mask);
    }
    AffinityGuard(const AffinityGuard&) = delete;
    AffinityGuard& operator=(const AffinityGuard&) = delete;
};
#else
struct AffinityGuard {
    // No-op on non-Linux platforms
};
#endif

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// hybrid_for_range_impl — core orchestration
// ─────────────────────────────────────────────────────────────────────────────

void hybrid_for_range_impl(const char* loop_name_ptr,
                            std::size_t begin, std::size_t end,
                            FunctionRef<void(std::size_t, std::size_t, int)> body,
                            const Options& user_opts) {
    // ── 0. Setup ──────────────────────────────────────────────────────────────
    static std::once_flag trace_flag;
    std::call_once(trace_flag, trace_init);

    Runtime& rt = get_runtime();
    const Options& opts = user_opts;

    const std::string loop_name = loop_name_ptr ? loop_name_ptr : "__anonymous__";
    if (!loop_name_ptr) {
        util::warn_once("hybrid_for called with nullptr loop_name; "
                        "all anonymous loops share state.");
    }

    // ── 1. Early exits ────────────────────────────────────────────────────────
    if (begin >= end) return;
    const std::size_t N = end - begin;

    // Never nest inside an existing parallel region
    if (omp_in_parallel()) {
        // Run serially in the calling thread
        body(begin, end, 0);
        return;
    }

    // Determine effective slot CPUs (respect use_smt)
    const Topology& topo = rt.topo;
    std::vector<int> p_slots, e_slots;
    if (opts.use_smt) {
        p_slots = topo.p_cpus;
    } else {
        // Only primary P threads (sibling_rank == 0)
        for (const auto& ci : topo.cpus) {
            if (ci.type == CoreType::P && ci.sibling_rank <= 0)
                p_slots.push_back(ci.id);
        }
    }
    e_slots = topo.e_cpus;

    const int nP = static_cast<int>(p_slots.size());
    const int nE = static_cast<int>(e_slots.size());
    const int K = nP + nE;

    // Build ordered slot_cpus: P first, then E
    std::vector<int> slot_cpus;
    slot_cpus.reserve(static_cast<std::size_t>(K));
    for (int c : p_slots) slot_cpus.push_back(c);
    for (int c : e_slots) slot_cpus.push_back(c);

    // Tiny-loop serial threshold
    const std::size_t grain = std::max<std::size_t>(1, opts.grain);
    const std::size_t serial_threshold =
        static_cast<std::size_t>(2) * std::max(grain, static_cast<std::size_t>(K));
    if (N < serial_threshold || K <= 1) {
        body(begin, end, 0);
        return;
    }

    // Save calling thread's CPU affinity; restored on function exit
    // (after all parallel regions complete, including warm-up and main phase)
    AffinityGuard affinity_guard;
    (void)affinity_guard;

    // ── 2. Loop state lookup ──────────────────────────────────────────────────
    LoopState& ls = rt.loop_registry.get(loop_name);
    const bool need_calib = ls.needs_calibration(opts.recalibrate,
                                                   opts.recalibrate_every_n,
                                                   opts.drift_threshold);

    // ── 3. Warm-up (calibration) ──────────────────────────────────────────────
    // Auto min warmup iters: 8 * K, but never more than 20% of N
    const std::size_t min_wu_auto = static_cast<std::size_t>(8 * K);
    const std::size_t max_wu = static_cast<std::size_t>(0.20 * static_cast<double>(N));
    std::size_t W = 0;
    std::vector<double> slot_speeds(static_cast<std::size_t>(K), 1.0);

    const double call_t0 = now_s();
    double warmup_done_s = call_t0;

    if (need_calib) {
        const std::size_t min_wu =
            opts.min_warmup_iters > 0 ? opts.min_warmup_iters : min_wu_auto;

        W = static_cast<std::size_t>(
            std::ceil(opts.warmup_fraction * static_cast<double>(N)));
        W = std::max(W, min_wu);
        W = std::min(W, max_wu);
        // Round to multiple of grain
        W = (W / grain) * grain;
        if (W == 0) W = grain;
        W = std::min(W, N); // never exceed N

        CalibrateParams cp;
        cp.begin = begin;
        cp.end = begin + W;
        cp.grain = grain;
        cp.slot_cpus = &slot_cpus;
        cp.disable_pinning = util::getenv_bool("HS_DISABLE_PINNING");

        const auto speeds = calibrate(body, cp);
        for (std::size_t i = 0; i < static_cast<std::size_t>(K); ++i)
            slot_speeds[i] = speeds[i].iters_per_s;

        // Save to loop state
        ls.cached_speeds = slot_speeds;
        ls.ever_calibrated = true;
    } else {
        // Reuse cached speeds
        if (!ls.cached_speeds.empty() &&
            ls.cached_speeds.size() == static_cast<std::size_t>(K)) {
            slot_speeds = ls.cached_speeds;
        }
        W = 0;
    }
    warmup_done_s = now_s();

    // ── 4. Plan ───────────────────────────────────────────────────────────────
    const std::size_t R = N - W; // remaining iterations

    const double plan_t0 = now_s();
    PlannerInput pi;
    pi.slot_speeds = &slot_speeds;
    pi.n_p_slots = nP;
    pi.n_e_slots = nE;
    pi.n_p_phys = topo.n_p_phys;
    pi.n_remaining = R;
    pi.kappa = ls.kappa;
    pi.fork_join_by_threads = g_fork_join_overhead_s.empty()
                                  ? nullptr
                                  : g_fork_join_overhead_s.data();
    pi.policy = opts.policy;
    pi.fixed_ratio_p_over_e = opts.fixed_ratio_p_over_e;
    pi.min_parallel_seconds = opts.min_parallel_seconds;
    pi.power_model = &rt.power_model;

    const Plan plan = choose_plan(pi);
    const double plan_done_s = now_s();

    // Serial path (tiny remaining or planner says serial)
    if (plan.run_serial || R == 0) {
        if (R > 0) {
            const int slot = plan.serial_slot;
            const int cpu = (slot < static_cast<int>(slot_cpus.size()))
                                ? slot_cpus[static_cast<std::size_t>(slot)]
                                : -1;
            if (cpu >= 0) pin_current_thread(cpu);
            body(begin + W, end, slot);
        }
        if (opts.stats_out) {
            LoopStats& st = *opts.stats_out;
            st.n_iters = N;
            st.slots_used = 1;
            st.p_threads = 0;
            st.e_threads = 0;
            st.warmup_seconds = warmup_done_s - call_t0;
            st.plan_seconds = plan_done_s - plan_t0;
            st.main_seconds = now_s() - plan_done_s;
            st.total_seconds = now_s() - call_t0;
            st.pred_time_s = plan.pred_time_s;
            st.pred_power_w = plan.pred_power_w;
            st.pred_energy_j = plan.pred_energy_j;
            st.pred_edp = plan.pred_edp;
            st.meas_energy_j = std::numeric_limits<double>::quiet_NaN();
            st.calibrated_this_call = need_calib;
        }
        ls.call_count++;
        return;
    }

    // ── 5. Execute main phase ─────────────────────────────────────────────────
    const int K_chosen = plan.a + plan.b;

    // Build chosen slot cpu list: first plan.a P slots, then plan.b E slots
    std::vector<int> chosen_cpus;
    chosen_cpus.reserve(static_cast<std::size_t>(K_chosen));
    for (int i = 0; i < plan.a; ++i)
        chosen_cpus.push_back(p_slots[static_cast<std::size_t>(i)]);
    for (int j = 0; j < plan.b; ++j)
        chosen_cpus.push_back(e_slots[static_cast<std::size_t>(j)]);

    // Weights for chosen slots (sub-vector of plan.weights)
    const std::vector<double>& w = plan.weights;

    // Static split: (1 - tail_fraction) of R, rounded down to grain
    const std::size_t R_static_raw =
        static_cast<std::size_t>((1.0 - opts.tail_fraction) * static_cast<double>(R));
    const std::size_t R_static = (R_static_raw / grain) * grain;
    const std::size_t static_begin = begin + W;
    const std::size_t tail_begin = static_begin + R_static;

    // Compute static blocks
    const auto blocks =
        compute_static_blocks(static_begin, R_static, w, grain);

    // Dynamic tail setup
    DynamicTail dtail;
    {
        const std::size_t tail_len = end - tail_begin;
        const std::size_t tail_chunk =
            std::max(grain,
                     tail_len / static_cast<std::size_t>(8 * K_chosen + 1));
        dtail.init(tail_begin, end, tail_chunk, grain);
    }

    // Per-slot finish times
    std::vector<SlotFinish> slot_finish(static_cast<std::size_t>(K_chosen));

    // Exception capture
    std::exception_ptr captured_ex;
    std::mutex ex_mutex;

    // RAPL start
    double rapl_start_uj = std::numeric_limits<double>::quiet_NaN();
    if (rt.energy.available()) rapl_start_uj = rt.energy.read_package_uj();

    const double main_t0 = now_s();

    omp_set_dynamic(0);
#pragma omp parallel num_threads(K_chosen)
    {
        const int slot = omp_get_thread_num();
        if (slot < K_chosen) {
            // Pin
            const bool disable_pin = util::getenv_bool("HS_DISABLE_PINNING");
            if (!disable_pin) {
                pin_current_thread(chosen_cpus[static_cast<std::size_t>(slot)]);
            }

            try {
                // Static block
                const SlotBlock& blk = blocks[static_cast<std::size_t>(slot)];
                if (blk.end > blk.begin) {
                    body(blk.begin, blk.end, slot);
                }
                slot_finish[static_cast<std::size_t>(slot)].static_done_s = now_s();

                // Dynamic tail
                while (true) {
                    auto [lo, hi] = dtail.next_chunk();
                    if (lo >= hi) break;
                    body(lo, hi, slot);
                }
                slot_finish[static_cast<std::size_t>(slot)].finish_s = now_s();

            } catch (...) {
                std::lock_guard<std::mutex> lock(ex_mutex);
                if (!captured_ex) captured_ex = std::current_exception();
                // Stop handing out work by exhausting the tail counter
                dtail.counter.store(end + 1, std::memory_order_relaxed);
            }
        }
    }

    const double main_done_s = now_s();
    const double meas_t = main_done_s - main_t0;

    // RAPL end
    double meas_energy_j = std::numeric_limits<double>::quiet_NaN();
    const double total_elapsed = main_done_s - call_t0;
    if (rt.energy.available() && !std::isnan(rapl_start_uj)) {
        if (rt.energy.window_ok(total_elapsed)) {
            const double rapl_end_uj = rt.energy.read_package_uj();
            if (!std::isnan(rapl_end_uj)) {
                // Find max_range from the domains list
                std::uint64_t max_range = 0;
                const auto& doms = rt.energy.domains();
                for (const auto& d : doms) {
                    if (d.name == "package-0") { max_range = d.max_range_uj; break; }
                }
                const double delta_uj = EnergyReader::delta_uj(
                    static_cast<std::uint64_t>(rapl_start_uj),
                    static_cast<std::uint64_t>(rapl_end_uj), max_range);
                meas_energy_j = delta_uj * 1e-6;
            }
        }
    }

    // ── 6. Imbalance ─────────────────────────────────────────────────────────
    double t_max = 0.0, t_sum = 0.0;
    for (int s = 0; s < K_chosen; ++s) {
        const double ft = slot_finish[static_cast<std::size_t>(s)].finish_s - main_t0;
        t_max = std::max(t_max, ft);
        t_sum += ft;
    }
    const double t_mean = K_chosen > 0 ? t_sum / static_cast<double>(K_chosen) : 0.0;
    const double imbalance =
        (t_max > 0.0) ? (t_max - t_mean) / t_max : 0.0;

    // ── 7. Online kappa refinement ────────────────────────────────────────────
    if (!std::isnan(meas_energy_j) && meas_t > 0.0 &&
        rt.energy.window_ok(total_elapsed)) {
        const double P_meas = meas_energy_j / meas_t;
        const double P_model =
            rt.power_model.predict_w(plan.a, plan.b, topo.n_p_phys);
        if (P_model > 0.0) {
            const double kappa_new = P_meas / P_model;
            ls.update_kappa(kappa_new);
        }
    }

    // ── 8. Update loop state ──────────────────────────────────────────────────
    const double pred_err =
        (meas_t > 0.0)
            ? std::abs(plan.pred_time_s - meas_t) / meas_t
            : 0.0;

    ls.last_imbalance = imbalance;
    ls.last_pred_err = pred_err;
    ls.last_a = plan.a;
    ls.last_b = plan.b;
    ls.call_count++;

    // ── 9. Fill LoopStats ─────────────────────────────────────────────────────
    if (opts.stats_out) {
        LoopStats& st = *opts.stats_out;
        st.n_iters = N;
        st.slots_used = K_chosen;
        st.p_threads = plan.a;
        st.e_threads = plan.b;
        st.warmup_seconds = warmup_done_s - call_t0;
        st.plan_seconds = plan_done_s - plan_t0;
        st.main_seconds = meas_t;
        st.total_seconds = total_elapsed;
        st.pred_time_s = plan.pred_time_s;
        st.pred_power_w = plan.pred_power_w;
        st.pred_energy_j = plan.pred_energy_j;
        st.pred_edp = plan.pred_edp;
        st.meas_energy_j = meas_energy_j;
        st.imbalance_percent = imbalance * 100.0;
        st.slot_speed_iters_per_s = slot_speeds;
        st.slot_finish_s.resize(static_cast<std::size_t>(K_chosen));
        for (int s = 0; s < K_chosen; ++s)
            st.slot_finish_s[static_cast<std::size_t>(s)] =
                slot_finish[static_cast<std::size_t>(s)].finish_s - main_t0;
        st.calibrated_this_call = need_calib;
    }

    // ── 10. CSV trace ─────────────────────────────────────────────────────────
    trace_row(loop_name, ls.call_count, need_calib, N, plan.a, plan.b,
              plan.pred_time_s, plan.pred_energy_j, plan.pred_edp,
              meas_t, meas_energy_j, imbalance * 100.0);

    // ── Rethrow any captured exception ───────────────────────────────────────
    if (captured_ex) std::rethrow_exception(captured_ex);
}

// ─────────────────────────────────────────────────────────────────────────────
// Runtime control API
// ─────────────────────────────────────────────────────────────────────────────

RuntimeInfo runtime_info() {
    const Runtime& rt = get_runtime();
    RuntimeInfo ri;
    ri.n_p_slots = rt.topo.n_p_slots();
    ri.n_e_slots = rt.topo.n_e_slots();
    ri.n_p_phys = rt.topo.n_p_phys;
    ri.smt_active = rt.topo.smt_active;
    ri.hybrid_cpu = rt.topo.hybrid;
    ri.rapl_available = rt.energy.available();
    ri.power_profile_loaded = rt.power_model.profile_loaded();
    ri.n_slots = rt.topo.n_slots();
    ri.topology_description = describe_topology(rt.topo);
    return ri;
}

void set_default_options(const Options& opts) {
    get_runtime().default_opts = opts;
}

void reset_loop_state() {
    get_runtime().loop_registry.reset();
}

void shutdown() {
    // Nothing requiring explicit cleanup; singletons are destroyed at program exit.
}

} // namespace hs
