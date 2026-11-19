/// @file power_profile.cpp
/// hs_power_profile: measures RAPL power at varying thread counts and fits the power model.

#include "hybrid_sched/energy.hpp"
#include "hybrid_sched/power_model.hpp"
#include "hybrid_sched/timer.hpp"
#include "hybrid_sched/topology.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <omp.h>

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [--out profile.txt] [--settle 1.0] [--measure 2.0]\n"
              << "  Measures idle + incremental power per P-physical, P-SMT and E thread.\n"
              << "  Requires RAPL access (run scripts/rapl_permissions.sh if needed).\n";
}

/// Compute-bound register FMA loop — prevent elimination with a volatile store.
static double compute_workload(volatile double* out) {
    double acc = 1.0;
    for (int i = 0; i < 50000000; ++i) acc = acc * 1.000001 + 0.000001;
    *out = acc;
    return acc;
}

/// Measure average package power over duration_s seconds with n_threads pinned to cpus.
static double measure_power(int n_threads, const std::vector<int>& cpus,
                              hs::EnergyReader& energy,
                              double settle_s, double measure_s) {
    if (!energy.available()) return std::numeric_limits<double>::quiet_NaN();

    // Settle: run for settle_s with threads active
    {
        volatile double sink = 0.0;
        omp_set_dynamic(0);
#pragma omp parallel num_threads(n_threads)
        {
            const int tid = omp_get_thread_num();
            if (tid < static_cast<int>(cpus.size()))
                hs::pin_current_thread(cpus[static_cast<std::size_t>(tid)]);

            const double t_end = hs::now_s() + settle_s;
            while (hs::now_s() < t_end)
                compute_workload(const_cast<volatile double*>(&sink));
        }
    }

    // Measure
    const double uj_before = energy.read_package_uj();
    const double t0 = hs::now_s();

    {
        volatile double sink = 0.0;
        omp_set_dynamic(0);
#pragma omp parallel num_threads(n_threads)
        {
            const int tid = omp_get_thread_num();
            if (tid < static_cast<int>(cpus.size()))
                hs::pin_current_thread(cpus[static_cast<std::size_t>(tid)]);

            const double t_end = hs::now_s() + measure_s;
            while (hs::now_s() < t_end)
                compute_workload(const_cast<volatile double*>(&sink));
        }
    }

    const double elapsed = hs::now_s() - t0;
    const double uj_after = energy.read_package_uj();

    if (std::isnan(uj_before) || std::isnan(uj_after)) return std::numeric_limits<double>::quiet_NaN();

    const double delta_j = hs::EnergyReader::delta_uj(
        static_cast<std::uint64_t>(uj_before),
        static_cast<std::uint64_t>(uj_after), 0) * 1e-6;
    return delta_j / elapsed;
}

/// Simple least-squares fit for y = a * x (no intercept).
static double fit_slope(const std::vector<double>& x, const std::vector<double>& y) {
    double num = 0.0, den = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        num += x[i] * y[i];
        den += x[i] * x[i];
    }
    return (den > 0.0) ? num / den : 0.0;
}

int main(int argc, char** argv) {
    double settle_s  = 1.0;
    double measure_s = 2.0;
    std::string out_path = "./power_profile.txt";

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if ((arg == "--out" || arg == "-o") && i + 1 < argc)
            out_path = argv[++i];
        else if (arg == "--settle" && i + 1 < argc)
            settle_s = std::stod(argv[++i]);
        else if (arg == "--measure" && i + 1 < argc)
            measure_s = std::stod(argv[++i]);
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
    }

    hs::EnergyReader energy;
    if (!energy.available()) {
        std::cerr << "RAPL not available. Cannot measure power.\n"
                  << "Run scripts/rapl_permissions.sh and try again.\n";
        return 1;
    }

    const hs::Topology topo = hs::detect_topology();
    std::cout << "Topology: " << hs::describe_topology(topo) << "\n\n";

    // ── Idle power (5 s) ──────────────────────────────────────────────────────
    std::cout << "Measuring idle power (5 s)...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1)); // settle
    const double idle_w = energy.measure_idle_power_w(5.0);
    std::cout << "  Idle power: " << idle_w << " W\n\n";

    // ── P-physical cores ──────────────────────────────────────────────────────
    std::vector<double> p_phys_powers;
    std::vector<int> p_primary_cpus;
    for (const auto& ci : topo.cpus)
        if (ci.type == hs::CoreType::P && ci.sibling_rank <= 0)
            p_primary_cpus.push_back(ci.id);

    std::cout << "Measuring P-physical cores (1.." << p_primary_cpus.size() << ")...\n";
    for (std::size_t n = 1; n <= p_primary_cpus.size(); ++n) {
        std::vector<int> cpus(p_primary_cpus.begin(),
                               p_primary_cpus.begin() + static_cast<std::ptrdiff_t>(n));
        const double pw = measure_power(static_cast<int>(n), cpus, energy, settle_s, measure_s);
        std::cout << "  " << n << " P-phys: " << pw << " W (incremental: "
                  << pw - idle_w << " W)\n";
        p_phys_powers.push_back(pw - idle_w);
    }

    // ── P-SMT siblings ────────────────────────────────────────────────────────
    std::vector<double> p_smt_powers;
    std::vector<int> smt_cpus;
    for (const auto& ci : topo.cpus)
        if (ci.type == hs::CoreType::P && ci.sibling_rank > 0)
            smt_cpus.push_back(ci.id);

    if (!smt_cpus.empty()) {
        std::cout << "\nMeasuring P-SMT siblings...\n";
        // First: all primaries active
        const double base_all_primary = measure_power(
            static_cast<int>(p_primary_cpus.size()), p_primary_cpus, energy, settle_s, measure_s);
        std::cout << "  All primaries: " << base_all_primary << " W\n";

        for (std::size_t n = 1; n <= smt_cpus.size(); ++n) {
            std::vector<int> cpus = p_primary_cpus;
            cpus.insert(cpus.end(), smt_cpus.begin(),
                        smt_cpus.begin() + static_cast<std::ptrdiff_t>(n));
            const double pw = measure_power(static_cast<int>(cpus.size()), cpus, energy,
                                            settle_s, measure_s);
            std::cout << "  +" << n << " SMT: " << pw << " W"
                      << " (incremental over all-primary: " << pw - base_all_primary << " W)\n";
            p_smt_powers.push_back(pw - base_all_primary);
        }
    }

    // ── E-cores ───────────────────────────────────────────────────────────────
    std::vector<double> e_powers;
    std::cout << "\nMeasuring E-cores (1.." << topo.e_cpus.size() << ")...\n";
    for (std::size_t n = 1; n <= topo.e_cpus.size(); ++n) {
        std::vector<int> cpus(topo.e_cpus.begin(),
                               topo.e_cpus.begin() + static_cast<std::ptrdiff_t>(n));
        const double pw = measure_power(static_cast<int>(n), cpus, energy, settle_s, measure_s);
        std::cout << "  " << n << " E: " << pw << " W (incremental: "
                  << pw - idle_w << " W)\n";
        e_powers.push_back(pw - idle_w);
    }

    // ── Fit model ─────────────────────────────────────────────────────────────
    // p_phys_w: slope of (power - idle) vs n_p_phys
    std::vector<double> x_phys(p_phys_powers.size());
    std::iota(x_phys.begin(), x_phys.end(), 1.0);
    const double p_phys_w = p_phys_powers.empty() ? 4.0 : fit_slope(x_phys, p_phys_powers);

    std::vector<double> x_smt(p_smt_powers.size());
    std::iota(x_smt.begin(), x_smt.end(), 1.0);
    const double p_smt_w = p_smt_powers.empty() ? 0.8 : fit_slope(x_smt, p_smt_powers);

    std::vector<double> x_e(e_powers.size());
    std::iota(x_e.begin(), x_e.end(), 1.0);
    const double p_e_w = e_powers.empty() ? 1.0 : fit_slope(x_e, e_powers);

    std::cout << "\n=== Fitted model ===\n";
    std::cout << "  base_w    = " << idle_w   << "\n";
    std::cout << "  p_phys_w  = " << p_phys_w << "\n";
    std::cout << "  p_smt_w   = " << p_smt_w  << "\n";
    std::cout << "  p_e_w     = " << p_e_w    << "\n";
    std::cout << "\nWARNING: PL1/PL2 power limits cause non-linearity at high occupancy.\n"
              << "Re-run in the same thermal/power mode as experiments.\n\n";

    // ── Write profile ─────────────────────────────────────────────────────────
    hs::PowerProfile prof;
    prof.base_w      = idle_w;
    prof.p_phys_w    = p_phys_w;
    prof.p_smt_w     = p_smt_w;
    prof.p_e_w       = p_e_w;
    prof.n_p_phys    = topo.n_p_phys;
    prof.n_p_logical = topo.n_p_slots();
    prof.n_e         = topo.n_e_slots();
    prof.profile_loaded = true;

    // Timestamp
    {
        char buf[64];
        const std::time_t t = std::time(nullptr);
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&t));
        prof.timestamp = buf;
    }

    // CPU model
    prof.cpu_model = hs::util::read_first_line("/proc/cpuinfo");
    {
        const auto colon = prof.cpu_model.find(':');
        if (colon != std::string::npos)
            prof.cpu_model = hs::util::trim(prof.cpu_model.substr(colon + 1));
    }

    // Write profile file directly (bypass PowerModel which has its own internal profile)
    std::ofstream out_file(out_path);
    if (out_file.is_open()) {
        out_file << hs::serialize_power_profile(prof);
        out_file.close();
        std::cout << "Profile written to " << out_path << "\n";
    } else {
        std::cerr << "WARNING: could not write profile to " << out_path << "\n";
    }

    // Also print directly
    std::cout << hs::serialize_power_profile(prof);

    return 0;
}
