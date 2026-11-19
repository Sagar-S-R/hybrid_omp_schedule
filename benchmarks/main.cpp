/// @file main.cpp
/// hs_bench entry point — sub-command dispatcher.

#include "common/cli.hpp"
#include "common/harness.hpp"
#include "common/csv_writer.hpp"
#include "executors/hybrid_executor.hpp"
#include "executors/omp_executors.hpp"
#include "executors/fixed_ratio_executor.hpp"
#include "kernels/kernel.hpp"
#include "hybrid_sched/hybrid_for.hpp"
#include "hybrid_sched/topology.hpp"
#include "hybrid_sched/util.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace hs { namespace bench {
    int cmd_sweep(const CLI& cli);
    int cmd_compare(const CLI& cli);
    int cmd_overhead(const CLI& cli);
    int cmd_oracle(const CLI& cli);
    std::vector<std::string> list_kernel_names();
    std::unique_ptr<Kernel> make_kernel(const std::string& name);
}}

static void print_help(const char* prog) {
    std::cout <<
        "Usage: " << prog << " <subcommand> [options]\n\n"
        "Subcommands:\n"
        "  list                        List kernels, variants, topology and RAPL status\n"
        "  run     --kernel K --variant V [--size S] [--reps N] [--warmup-reps W]\n"
        "          [--threads T] [--chunk C] [--affinity A] [--out file.csv]\n"
        "          [--cooldown-ms M] [--seed X] [--verify]\n"
        "  sweep   --kernel K|all [--size S] [--reps N] [--out file.csv]\n"
        "  compare --kernel K|all [--size S] [--reps N] [--out file.csv] [--cooldown-ms M]\n"
        "  overhead [--reps N] [--out file.csv]\n"
        "  oracle  --kernel K|all [--size S] [--reps N] [--out file.csv]\n\n"
        "Options for run:\n"
        "  --variant: omp_static, omp_dynamic, omp_guided, hybrid_edp,\n"
        "             hybrid_time_balanced, hybrid_energy, hybrid_p_only,\n"
        "             hybrid_e_only, fixed_ratio\n"
        "  --affinity: OS, ORDERED, P_ONLY, E_ONLY\n"
        "  --size: tiny, small, medium (default), large\n\n"
        "Environment variables:\n"
        "  HS_VERBOSE=1         Enable verbose logging\n"
        "  HS_TRACE_CSV=path    Append per-call trace rows to this CSV\n"
        "  HS_P_CPUS=cpulist    Override P-core CPU list\n"
        "  HS_E_CPUS=cpulist    Override E-core CPU list\n"
        "  HS_POWER_PROFILE=path Load power profile from path\n"
        "  HS_FORCE_HOMOGENEOUS=1 Treat all CPUs as P-cores\n"
        "  HS_DISABLE_PINNING=1 Disable thread pinning\n"
        "  HS_RAPL_MIN_MS=N     Minimum RAPL window in ms (default 20)\n";
}

static void cmd_list() {
    const hs::RuntimeInfo ri = hs::runtime_info();
    std::cout << "=== Topology ===\n" << ri.topology_description << "\n\n";
    std::cout << "RAPL available: " << (ri.rapl_available ? "yes" : "no") << "\n";
    std::cout << "Power profile loaded: " << (ri.power_profile_loaded ? "yes" : "no") << "\n\n";
    std::cout << "=== Kernels ===\n";
    for (const auto& k : hs::bench::list_kernel_names())
        std::cout << "  " << k << "\n";
    std::cout << "\n=== Variants ===\n"
              << "  omp_static, omp_dynamic, omp_guided\n"
              << "  hybrid_edp, hybrid_energy, hybrid_time_balanced\n"
              << "  hybrid_p_only, hybrid_e_only, fixed_ratio\n";
}

static int cmd_run(const hs::bench::CLI& cli) {
    const std::string kname    = cli.get("kernel", "stream_triad");
    const std::string variant  = cli.get("variant", "hybrid_edp");
    const std::string size_str = cli.get("size", "medium");
    const int reps             = cli.get_int("reps", 10);
    const int warmup_reps      = cli.get_int("warmup-reps", 2);
    const int threads          = cli.get_int("threads", 0);
    const int chunk            = cli.get_int("chunk", 0);
    const std::string aff_str  = cli.get("affinity", "ORDERED");
    const std::string out_path = cli.get("out", "");
    const int cooldown_ms      = cli.get_int("cooldown-ms", 500);
    const std::uint64_t seed   = static_cast<std::uint64_t>(cli.get_long("seed", 42));
    const bool do_verify       = cli.flag("verify");

    hs::bench::SizeClass sc = hs::bench::SizeClass::Medium;
    if (size_str == "tiny")  sc = hs::bench::SizeClass::Tiny;
    if (size_str == "small") sc = hs::bench::SizeClass::Small;
    if (size_str == "large") sc = hs::bench::SizeClass::Large;

    hs::bench::AffinityMode aff = hs::bench::AffinityMode::Ordered;
    if (aff_str == "OS")     aff = hs::bench::AffinityMode::OS;
    if (aff_str == "P_ONLY") aff = hs::bench::AffinityMode::P_Only;
    if (aff_str == "E_ONLY") aff = hs::bench::AffinityMode::E_Only;

    const int topo_threads = threads > 0 ? threads : hs::detect_topology().n_slots();

    // Build executor
    std::unique_ptr<hs::bench::Executor> ex;
    if (variant == "omp_static")
        ex = std::make_unique<hs::bench::OmpExecutor>(hs::bench::OmpKind::Static, chunk, topo_threads, aff);
    else if (variant == "omp_dynamic")
        ex = std::make_unique<hs::bench::OmpExecutor>(hs::bench::OmpKind::Dynamic, chunk, topo_threads, aff);
    else if (variant == "omp_guided")
        ex = std::make_unique<hs::bench::OmpExecutor>(hs::bench::OmpKind::Guided, chunk, topo_threads, aff);
    else if (variant == "hybrid_edp")
        ex = std::make_unique<hs::bench::HybridExecutor>(hs::Policy::EDP, hs::Recalibrate::EVERY_CALL);
    else if (variant == "hybrid_energy")
        ex = std::make_unique<hs::bench::HybridExecutor>(hs::Policy::ENERGY, hs::Recalibrate::EVERY_CALL);
    else if (variant == "hybrid_time_balanced")
        ex = std::make_unique<hs::bench::HybridExecutor>(hs::Policy::TIME_BALANCED, hs::Recalibrate::EVERY_CALL);
    else if (variant == "hybrid_p_only")
        ex = std::make_unique<hs::bench::HybridExecutor>(hs::Policy::P_ONLY, hs::Recalibrate::EVERY_CALL);
    else if (variant == "hybrid_e_only")
        ex = std::make_unique<hs::bench::HybridExecutor>(hs::Policy::E_ONLY, hs::Recalibrate::EVERY_CALL);
    else if (variant == "fixed_ratio")
        ex = std::make_unique<hs::bench::FixedRatioExecutor>(2.0);
    else {
        std::cerr << "Unknown variant: " << variant << "\n";
        return 1;
    }

    auto kernel = hs::bench::make_kernel(kname);
    hs::bench::KernelParams kp;
    kp.size_class = sc;
    kp.seed = seed;
    kernel->setup(kp);

    hs::bench::HarnessParams hp;
    hp.reps        = reps;
    hp.warmup_reps = warmup_reps;
    hp.cooldown_ms = cooldown_ms;
    hp.verify      = do_verify;

    const auto records = hs::bench::run_kernel(*kernel, *ex, kp, hp);

    // Print summary to stdout
    double t_sum = 0.0, e_sum = 0.0;
    for (const auto& r : records) {
        t_sum += r.time_s;
        if (!std::isnan(r.energy_j_gross)) e_sum += r.energy_j_gross;
        std::cout << "rep=" << r.rep << " time=" << r.time_s << "s";
        if (!std::isnan(r.energy_j_gross))
            std::cout << " energy=" << r.energy_j_gross << "J"
                      << " EDP=" << r.edp;
        if (do_verify) std::cout << " verified=" << (r.verified ? "OK" : "FAIL");
        std::cout << "\n";
    }
    const double t_mean = t_sum / static_cast<double>(records.size());
    std::cout << "Mean time: " << t_mean << "s\n";

    // Write CSV if requested
    if (!out_path.empty()) {
        hs::bench::CsvWriter csv(out_path);
        csv.write_header(hs::bench::run_record_headers());
        for (const auto& r : records)
            csv.write_row(hs::bench::run_record_to_csv(r));
        std::cout << "Results written to " << out_path << "\n";
    }

    return (do_verify && !records.empty() && !records[0].verified) ? 1 : 0;
}

int main(int argc, char** argv) {
    hs::bench::CLI cli;
    cli.parse(argc, argv);

    const std::string sub = cli.subcommand();

    if (sub.empty() || sub == "help" || cli.flag("help")) {
        print_help(argv[0]);
        return 0;
    }
    if (sub == "list") {
        cmd_list();
        return 0;
    }
    if (sub == "run")     return cmd_run(cli);
    if (sub == "sweep")   return hs::bench::cmd_sweep(cli);
    if (sub == "compare") return hs::bench::cmd_compare(cli);
    if (sub == "overhead")return hs::bench::cmd_overhead(cli);
    if (sub == "oracle")  return hs::bench::cmd_oracle(cli);

    std::cerr << "Unknown subcommand: " << sub << "\n";
    print_help(argv[0]);
    return 1;
}
