/// @file compare.cpp
/// hs_bench compare: all variants with randomised order per rep block + cooldowns.

#include "common/cli.hpp"
#include "common/csv_writer.hpp"
#include "common/harness.hpp"
#include "common/rng.hpp"
#include "executors/fixed_ratio_executor.hpp"
#include "executors/hybrid_executor.hpp"
#include "executors/omp_executors.hpp"
#include "kernels/kernel.hpp"
#include "hybrid_sched/energy.hpp"
#include "hybrid_sched/timer.hpp"
#include "hybrid_sched/topology.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

namespace hs {
namespace bench {

std::unique_ptr<Kernel> make_kernel(const std::string& name);
std::vector<std::string> list_kernel_names();

int cmd_compare(const CLI& cli) {
    const std::string kernel_arg = cli.get("kernel", "stream_triad");
    const std::string size_str   = cli.get("size", "medium");
    const int reps               = cli.get_int("reps", 10);
    const std::string out_path   = cli.get("out", "results/raw/compare.csv");
    const int cooldown_ms        = cli.get_int("cooldown-ms", 1000);

    SizeClass sc = SizeClass::Medium;
    if (size_str == "tiny")  sc = SizeClass::Tiny;
    if (size_str == "small") sc = SizeClass::Small;
    if (size_str == "large") sc = SizeClass::Large;

    std::vector<std::string> kernel_names;
    if (kernel_arg == "all") kernel_names = list_kernel_names();
    else                     kernel_names = {kernel_arg};

    const Topology topo = detect_topology();
    const int all_threads = topo.n_slots();

    // Build executor set
    // Using a struct to hold label + factory to avoid virtual dispatch issues with shuffling
    struct ExDesc {
        std::string label;
        std::function<std::unique_ptr<Executor>()> make;
    };

    std::vector<ExDesc> executor_descs;

    // OMP baselines
    for (OmpKind kind : {OmpKind::Static, OmpKind::Dynamic, OmpKind::Guided}) {
        executor_descs.push_back({
            "",
            [kind, all_threads]() {
                return std::make_unique<OmpExecutor>(kind, 0, all_threads, AffinityMode::Ordered);
            }
        });
    }
    // OMP P-only
    executor_descs.push_back({"", [&topo]() {
        const int pt = static_cast<int>(topo.p_cpus.size());
        return std::make_unique<OmpExecutor>(OmpKind::Static, 0, pt, AffinityMode::P_Only);
    }});
    // OMP E-only (if E cores exist)
    if (!topo.e_cpus.empty()) {
        executor_descs.push_back({"", [&topo]() {
            const int et = static_cast<int>(topo.e_cpus.size());
            return std::make_unique<OmpExecutor>(OmpKind::Static, 0, et, AffinityMode::E_Only);
        }});
    }
    // Fixed ratio
    executor_descs.push_back({"", []() {
        return std::make_unique<FixedRatioExecutor>(2.0);
    }});
    // Hybrid variants
    executor_descs.push_back({"", []() {
        return std::make_unique<HybridExecutor>(Policy::TIME_BALANCED, Recalibrate::EVERY_CALL);
    }});
    executor_descs.push_back({"", []() {
        return std::make_unique<HybridExecutor>(Policy::EDP, Recalibrate::EVERY_CALL);
    }});
    executor_descs.push_back({"", []() {
        return std::make_unique<HybridExecutor>(Policy::EDP, Recalibrate::ONCE);
    }});
    executor_descs.push_back({"", []() {
        return std::make_unique<HybridExecutor>(Policy::ENERGY, Recalibrate::EVERY_CALL);
    }});

    CsvWriter csv(out_path);
    csv.write_header(run_record_headers());

    rng::Xoshiro256 rng(12345);

    for (const auto& kname : kernel_names) {
        auto kernel = make_kernel(kname);
        KernelParams kp;
        kp.size_class = sc;
        kp.seed = 42;
        kernel->setup(kp);

        // Warm-up runs
        for (int w = 0; w < 2; ++w) {
            auto ex = executor_descs[0].make();
            kernel->reset();
            kernel->run(*ex);
        }

        // Per-rep: shuffle executor order to avoid thermal-drift bias
        std::vector<std::size_t> order(executor_descs.size());
        std::iota(order.begin(), order.end(), 0);

        for (int rep = 0; rep < reps; ++rep) {
            // Fisher-Yates shuffle
            for (std::size_t i = order.size() - 1; i > 0; --i) {
                const std::size_t j = static_cast<std::size_t>(
                    rng.next_bounded(static_cast<std::uint64_t>(i + 1)));
                std::swap(order[i], order[j]);
            }

            for (std::size_t idx : order) {
                auto ex = executor_descs[idx].make();
                kernel->reset();

                // Cooldown between runs
                std::this_thread::sleep_for(std::chrono::milliseconds(cooldown_ms));

                const SystemState pre = probe_system();

                EnergyReader energy_reader;
                double rapl_before = std::numeric_limits<double>::quiet_NaN();
                if (energy_reader.available())
                    rapl_before = energy_reader.read_package_uj();

                const double t0 = now_s();
                kernel->run(*ex);
                const double elapsed = now_s() - t0;

                RunRecord r;
                r.kernel_name    = kname;
                r.variant        = ex->label();
                r.size_class_str = (sc == SizeClass::Tiny)   ? "tiny"   :
                                   (sc == SizeClass::Small)  ? "small"  :
                                   (sc == SizeClass::Medium) ? "medium" : "large";
                r.rep            = rep;
                r.time_s         = elapsed;
                r.temp_c_before  = pre.pkg_temp_c;
                r.governor       = pre.governor;
                r.work_units     = kernel->work_units();
                r.work_unit_name = kernel->work_unit_name();

                if (energy_reader.available() && !std::isnan(rapl_before) &&
                    energy_reader.window_ok(elapsed)) {
                    const double after = energy_reader.read_package_uj();
                    const double delta = EnergyReader::delta_uj(
                        static_cast<std::uint64_t>(rapl_before),
                        static_cast<std::uint64_t>(after), 0);
                    r.energy_j_gross = delta * 1e-6;
                    r.avg_power_w    = r.energy_j_gross / elapsed;
                    r.edp            = r.energy_j_gross * elapsed;
                }

                const SystemState post = probe_system();
                r.temp_c_after = post.pkg_temp_c;
                r.p_khz_after  = post.p_core_khz;
                r.e_khz_after  = post.e_core_khz;

                if (r.work_units > 0.0 && elapsed > 0.0)
                    r.throughput = r.work_units / elapsed;

                // Read imbalance from executor
                r.imbalance_pct = ex->last_imbalance_pct();

                // Verify by default on first rep of each variant
                if (rep == 0) r.verified = kernel->verify();

                csv.write_row(run_record_to_csv(r));
                csv.flush();

                std::cerr << "[compare] rep=" << rep << " " << kname
                          << " " << ex->label()
                          << " t=" << elapsed << "s\n";
            }
        }
    }
    std::cout << "Compare complete. Output: " << out_path << "\n";
    return 0;
}

} // namespace bench
} // namespace hs
