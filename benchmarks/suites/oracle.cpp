/// @file oracle.cpp
/// hs_bench oracle: exhaustive (a,b) search = best achievable EDP per kernel.

#include "common/cli.hpp"
#include "common/csv_writer.hpp"
#include "common/harness.hpp"
#include "executors/hybrid_executor.hpp"
#include "executors/omp_executors.hpp"
#include "kernels/kernel.hpp"
#include "hybrid_sched/hybrid_for.hpp"
#include "hybrid_sched/planner.hpp"
#include "hybrid_sched/topology.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace hs {
namespace bench {

std::unique_ptr<Kernel> make_kernel(const std::string& name);
std::vector<std::string> list_kernel_names();

int cmd_oracle(const CLI& cli) {
    const std::string kernel_arg = cli.get("kernel", "stream_triad");
    const std::string size_str   = cli.get("size", "medium");
    const int reps               = cli.get_int("reps", 5);
    const std::string out_path   = cli.get("out", "results/raw/oracle.csv");

    SizeClass sc = SizeClass::Medium;
    if (size_str == "tiny")  sc = SizeClass::Tiny;
    if (size_str == "small") sc = SizeClass::Small;
    if (size_str == "large") sc = SizeClass::Large;

    std::vector<std::string> kernel_names;
    if (kernel_arg == "all") kernel_names = list_kernel_names();
    else                     kernel_names = {kernel_arg};

    const Topology topo = detect_topology();
    const int nP = topo.n_p_slots();
    const int nE = topo.n_e_slots();

    std::vector<std::string> headers = run_record_headers();
    headers.push_back("oracle_a");
    headers.push_back("oracle_b");
    CsvWriter csv(out_path);
    csv.write_header(headers);

    for (const auto& kname : kernel_names) {
        std::cerr << "[oracle] kernel=" << kname << "\n";

        struct Config { int a; int b; };
        std::vector<Config> configs;
        for (int a = 0; a <= nP; ++a)
            for (int b = 0; b <= nE; ++b)
                if (a + b >= 1) configs.push_back({a, b});

        // For each config, run reps times with a forced HybridExecutor subset
        // Using Policy::EDP but with forced a,b via env override is not trivial;
        // instead we build an OmpExecutor with Mixed affinity for each (a,b).
        for (const auto& cfg : configs) {
            std::cerr << "  (a=" << cfg.a << ", b=" << cfg.b << ")\n";
            auto kernel = make_kernel(kname);
            KernelParams kp;
            kp.size_class = sc;
            kp.seed = 42;
            kernel->setup(kp);

            OmpExecutor ex(OmpKind::Static, 0, cfg.a + cfg.b,
                           AffinityMode::Mixed, cfg.a, cfg.b);

            HarnessParams hp;
            hp.reps        = reps;
            hp.warmup_reps = 1;
            hp.cooldown_ms = 300;
            hp.verify      = false;

            const auto records = run_kernel(*kernel, ex, kp, hp);
            for (auto r : records) {
                r.a_p_threads = cfg.a;
                r.b_e_threads = cfg.b;
                auto row = run_record_to_csv(r);
                row.push_back(std::to_string(cfg.a));
                row.push_back(std::to_string(cfg.b));
                csv.write_row(row);
            }
            csv.flush();
        }
    }
    std::cout << "Oracle search complete. Output: " << out_path << "\n";
    std::cout << "Use hs_analyze to compute oracle EDP and regret ratio.\n";
    return 0;
}

} // namespace bench
} // namespace hs
