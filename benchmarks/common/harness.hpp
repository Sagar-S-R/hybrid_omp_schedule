#pragma once
/// @file harness.hpp
/// @brief Run one (kernel, executor, size) N times and collect RunRecords.

#include "common/executor.hpp"
#include "common/system_probe.hpp"
#include "kernels/kernel.hpp"
#include "hybrid_sched/energy.hpp"
#include "hybrid_sched/timer.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace hs {
namespace bench {

/// One measured repetition.
struct RunRecord {
    std::string kernel_name;
    std::string variant;
    std::string size_class_str;
    int rep{0};

    double time_s{0.0};
    double energy_j_gross{std::numeric_limits<double>::quiet_NaN()};
    double energy_j_net{std::numeric_limits<double>::quiet_NaN()};
    double avg_power_w{std::numeric_limits<double>::quiet_NaN()};
    double edp{std::numeric_limits<double>::quiet_NaN()};
    double imbalance_pct{0.0};
    double calib_overhead_s{0.0};

    int a_p_threads{0};
    int b_e_threads{0};
    int n_recalib{0};
    double pred_err_pct{std::numeric_limits<double>::quiet_NaN()};

    double temp_c_before{-1.0};
    double temp_c_after{-1.0};
    long p_khz_after{0};
    long e_khz_after{0};
    std::string governor;
    bool verified{false};

    double work_units{0.0};
    std::string work_unit_name;
    double throughput{0.0};

    long timestamp_ms{0};
    std::string host_cpu;
};

struct HarnessParams {
    int reps{10};
    int warmup_reps{2};
    int cooldown_ms{500};
    bool verify{false};
    double idle_power_w{std::numeric_limits<double>::quiet_NaN()};
};

/// Run kernel under executor for hparams.reps measured repetitions.
/// Returns one RunRecord per measured repetition.
[[nodiscard]] std::vector<RunRecord> run_kernel(Kernel& kernel,
                                                 Executor& executor,
                                                 const KernelParams& kparams,
                                                 const HarnessParams& hparams);

/// CSV column headers matching RunRecord fields.
[[nodiscard]] std::vector<std::string> run_record_headers();

/// Serialize a RunRecord to CSV values (same order as headers).
[[nodiscard]] std::vector<std::string> run_record_to_csv(const RunRecord& r);

} // namespace bench
} // namespace hs
