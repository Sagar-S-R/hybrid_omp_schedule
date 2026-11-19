/// @file harness.cpp

#include "harness.hpp"
#include "hybrid_sched/energy.hpp"
#include "hybrid_sched/timer.hpp"
#include "hybrid_sched/util.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

namespace hs {
namespace bench {

static std::string size_class_str(SizeClass sc) {
    switch (sc) {
    case SizeClass::Tiny:   return "tiny";
    case SizeClass::Small:  return "small";
    case SizeClass::Medium: return "medium";
    case SizeClass::Large:  return "large";
    }
    return "unknown";
}

static std::string detect_cpu_model() {
#if defined(__linux__)
    const std::string content = util::read_file("/proc/cpuinfo");
    for (const auto& line : util::split(content, '\n')) {
        if (line.find("model name") != std::string::npos) {
            const auto colon = line.find(':');
            if (colon != std::string::npos)
                return util::trim(line.substr(colon + 1));
        }
    }
#endif
    return "unknown";
}

static long now_ms() {
    return static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::vector<RunRecord> run_kernel(Kernel& kernel, Executor& executor,
                                   const KernelParams& kparams,
                                   const HarnessParams& hparams) {
    static const std::string cpu_model = detect_cpu_model();
    EnergyReader energy;

    // Warm-up repetitions (not recorded)
    for (int w = 0; w < hparams.warmup_reps; ++w) {
        kernel.reset();
        executor.reset();
        kernel.run(executor);
    }

    std::vector<RunRecord> records;
    records.reserve(static_cast<std::size_t>(hparams.reps));

    for (int rep = 0; rep < hparams.reps; ++rep) {
        kernel.reset();
        executor.reset();

        // Cooldown
        if (rep > 0 && hparams.cooldown_ms > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(hparams.cooldown_ms));
        }

        RunRecord r;
        r.kernel_name    = kernel.name();
        r.variant        = executor.label();
        r.size_class_str = size_class_str(kparams.size_class);
        r.rep            = rep;
        r.host_cpu       = cpu_model;
        r.timestamp_ms   = now_ms();
        r.work_units     = kernel.work_units();
        r.work_unit_name = kernel.work_unit_name();

        // System state before
        const SystemState pre = probe_system();
        r.temp_c_before = pre.pkg_temp_c;
        r.governor      = pre.governor;

        // RAPL before
        double rapl_before_uj = std::numeric_limits<double>::quiet_NaN();
        if (energy.available())
            rapl_before_uj = energy.read_package_uj();

        // Timed run
        const double t0 = now_s();
        kernel.run(executor);
        const double t1 = now_s();
        r.time_s = t1 - t0;

        // RAPL after
        if (energy.available() && !std::isnan(rapl_before_uj) &&
            energy.window_ok(r.time_s)) {
            const double rapl_after_uj = energy.read_package_uj();
            if (!std::isnan(rapl_after_uj)) {
                const double delta = EnergyReader::delta_uj(
                    static_cast<std::uint64_t>(rapl_before_uj),
                    static_cast<std::uint64_t>(rapl_after_uj), 0);
                r.energy_j_gross = delta * 1e-6;
                if (!std::isnan(hparams.idle_power_w)) {
                    r.energy_j_net =
                        r.energy_j_gross - hparams.idle_power_w * r.time_s;
                }
                r.avg_power_w = r.energy_j_gross / r.time_s;
                r.edp = r.energy_j_gross * r.time_s;
            }
        }

        // System state after
        const SystemState post = probe_system();
        r.temp_c_after = post.pkg_temp_c;
        r.p_khz_after  = post.p_core_khz;
        r.e_khz_after  = post.e_core_khz;

        // Throughput
        if (r.work_units > 0.0 && r.time_s > 0.0)
            r.throughput = r.work_units / r.time_s;

        // Read imbalance from executor (measured during the run)
        r.imbalance_pct = executor.last_imbalance_pct();

        // Verify (every rep when requested)
        if (hparams.verify)
            r.verified = kernel.verify();

        records.push_back(r);
    }
    return records;
}

std::vector<std::string> run_record_headers() {
    return {
        "timestamp_ms", "host_cpu",
        "kernel", "size_class", "variant",
        "rep", "time_s",
        "energy_j_gross", "energy_j_net", "avg_power_w", "edp",
        "imbalance_pct", "calib_overhead_s",
        "a_p_threads", "b_e_threads", "n_recalib", "pred_err_pct",
        "temp_c_before", "temp_c_after", "p_khz_after", "e_khz_after",
        "governor", "verified", "work_units", "work_unit_name", "throughput"
    };
}

static std::string fmt(double v) {
    if (std::isnan(v)) return "NaN";
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

std::vector<std::string> run_record_to_csv(const RunRecord& r) {
    return {
        std::to_string(r.timestamp_ms),
        r.host_cpu,
        r.kernel_name,
        r.size_class_str,
        r.variant,
        std::to_string(r.rep),
        fmt(r.time_s),
        fmt(r.energy_j_gross),
        fmt(r.energy_j_net),
        fmt(r.avg_power_w),
        fmt(r.edp),
        fmt(r.imbalance_pct),
        fmt(r.calib_overhead_s),
        std::to_string(r.a_p_threads),
        std::to_string(r.b_e_threads),
        std::to_string(r.n_recalib),
        fmt(r.pred_err_pct),
        fmt(r.temp_c_before),
        fmt(r.temp_c_after),
        std::to_string(r.p_khz_after),
        std::to_string(r.e_khz_after),
        r.governor,
        r.verified ? "1" : "0",
        fmt(r.work_units),
        r.work_unit_name,
        fmt(r.throughput)
    };
}

} // namespace bench
} // namespace hs
