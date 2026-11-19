/// @file sweep.cpp
/// hs_bench sweep: full factorial schedule x chunk x threads x affinity

#include "common/cli.hpp"
#include "common/csv_writer.hpp"
#include "common/harness.hpp"
#include "executors/omp_executors.hpp"
#include "kernels/kernel.hpp"
#include "hybrid_sched/topology.hpp"
#include "hybrid_sched/util.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace hs {
namespace bench {

// Forward declaration from registry.cpp
std::unique_ptr<Kernel> make_kernel(const std::string& name);
std::vector<std::string> list_kernel_names();

int cmd_sweep(const CLI& cli) {
    const std::string kernel_arg = cli.get("kernel", "stream_triad");
    const std::string size_str   = cli.get("size", "medium");
    const int reps               = cli.get_int("reps", 5);
    const std::string out_path   = cli.get("out", "results/raw/sweep.csv");

    SizeClass sc = SizeClass::Medium;
    if (size_str == "tiny")   sc = SizeClass::Tiny;
    if (size_str == "small")  sc = SizeClass::Small;
    if (size_str == "large")  sc = SizeClass::Large;

    std::vector<std::string> kernel_names;
    if (kernel_arg == "all") kernel_names = list_kernel_names();
    else                     kernel_names = {kernel_arg};

    const Topology topo = detect_topology();
    const int max_threads = topo.n_slots();

    // Thread counts to try
    std::vector<int> thread_counts;
    for (int t = 1; t <= max_threads; t = (t < 4) ? t + 1 : t + 2)
        thread_counts.push_back(t);
    if (thread_counts.empty() || thread_counts.back() != max_threads)
        thread_counts.push_back(max_threads);

    const std::vector<OmpKind> kinds{OmpKind::Static, OmpKind::Dynamic, OmpKind::Guided};
    const std::vector<int> chunks{1, 16, 64, 256};
    const std::vector<AffinityMode> affinities{AffinityMode::OS, AffinityMode::Ordered,
                                                AffinityMode::P_Only, AffinityMode::E_Only};

    CsvWriter csv(out_path);
    csv.write_header(run_record_headers());

    int total = 0;
    for (const auto& kname : kernel_names) {
        for (OmpKind kind : kinds) {
            for (int chunk : chunks) {
                for (int threads : thread_counts) {
                    for (AffinityMode aff : affinities) {
                        // Skip nonsensical: E_Only with no E cores
                        if (aff == AffinityMode::E_Only && topo.e_cpus.empty()) continue;
                        // Skip P_Only with no P cores (shouldn't happen)
                        if (aff == AffinityMode::P_Only && topo.p_cpus.empty()) continue;

                        auto kernel = make_kernel(kname);
                        KernelParams kp;
                        kp.size_class = sc;
                        kp.seed = 42;
                        kernel->setup(kp);

                        OmpExecutor ex(kind, chunk, threads, aff);

                        HarnessParams hp;
                        hp.reps = reps;
                        hp.warmup_reps = 1;
                        hp.cooldown_ms = 200;
                        hp.verify = false;

                        std::cerr << "[sweep] " << kname << " " << ex.label() << "\n";
                        const auto records = run_kernel(*kernel, ex, kp, hp);
                        for (const auto& r : records)
                            csv.write_row(run_record_to_csv(r));
                        csv.flush();
                        ++total;
                    }
                }
            }
        }
    }
    std::cout << "Sweep complete: " << total << " configurations written to " << out_path << "\n";
    return 0;
}

} // namespace bench
} // namespace hs
