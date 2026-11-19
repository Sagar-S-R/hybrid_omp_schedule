/// @file overhead.cpp
/// hs_bench overhead: measure framework overhead vs loop duration (break-even).

#include "common/cli.hpp"
#include "common/csv_writer.hpp"
#include "executors/hybrid_executor.hpp"
#include "executors/omp_executors.hpp"
#include "kernels/kernel.hpp"
#include "hybrid_sched/timer.hpp"
#include "hybrid_sched/topology.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace hs {
namespace bench {

std::unique_ptr<Kernel> make_kernel(const std::string& name);

int cmd_overhead(const CLI& cli) {
    const std::string out_path = cli.get("out", "results/raw/overhead.csv");
    const int reps             = cli.get_int("reps", 20);

    const Topology topo = detect_topology();
    const int all_t = topo.n_slots();

    // Loop durations to sweep: 10us, 50us, 100us, 500us, 1ms, 5ms, 50ms, 500ms, 1s
    // We control duration by varying FMAs per iteration and N
    struct DurationConfig { std::size_t n; std::size_t fma_per_iter; std::string label; };
    const std::vector<DurationConfig> configs = {
        {16,      4,    "~10us"},
        {64,      8,    "~50us"},
        {128,    16,    "~100us"},
        {512,    32,    "~500us"},
        {1024,   64,    "~1ms"},
        {4096,   64,    "~5ms"},
        {32768,  64,    "~50ms"},
        {262144, 64,    "~500ms"},
    };

    // Two executors: omp_static (P-only, same CPU set as hybrid) and hybrid_edp (P-only)
    auto make_omp = [&]() {
        return std::make_unique<OmpExecutor>(OmpKind::Static, 0, all_t,
                                             AffinityMode::Ordered);
    };
    auto make_hybrid = [&]() {
        return std::make_unique<HybridExecutor>(Policy::EDP, Recalibrate::EVERY_CALL);
    };

    // CSV
    std::vector<std::string> headers = {
        "loop_duration_label", "variant", "rep",
        "loop_time_s", "framework_overhead_s", "overhead_pct"
    };
    CsvWriter csv(out_path);
    csv.write_header(headers);

    for (const auto& cfg : configs) {
        std::cerr << "[overhead] duration=" << cfg.label << "\n";

        for (int rep = 0; rep < reps; ++rep) {
            // For each variant, run the uniform_fma kernel with same params
            for (int variant = 0; variant < 2; ++variant) {
                auto kernel = make_kernel("uniform_fma");
                KernelParams kp;
                kp.size_class = SizeClass::Tiny;
                kp.seed = 42;
                kp.n = cfg.n;
                kp.k = cfg.fma_per_iter;
                kernel->setup(kp);

                std::unique_ptr<Executor> ex;
                if (variant == 0) ex = make_omp();
                else               ex = make_hybrid();

                // Warm-up
                kernel->run(*ex);
                kernel->reset();

                const double t0 = now_s();
                kernel->run(*ex);
                const double total = now_s() - t0;

                // Overhead estimate: hybrid - omp (we record both and let analysis diff them)
                // Here we record raw timing; analysis computes the ratio
                csv.write_row({
                    cfg.label,
                    ex->label(),
                    std::to_string(rep),
                    std::to_string(total),
                    "NaN",
                    "NaN"
                });
            }
        }
        csv.flush();
    }

    std::cout << "Overhead results written to " << out_path << "\n";
    std::cout << "NOTE: overhead = (hybrid_time - omp_static_time) / omp_static_time per duration.\n";
    std::cout << "Target: < 5% on loops >= 5 ms.\n";
    return 0;
}

} // namespace bench
} // namespace hs
