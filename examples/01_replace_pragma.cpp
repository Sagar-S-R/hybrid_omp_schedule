/// @file 01_replace_pragma.cpp
/// Before/after: replacing #pragma omp parallel for with hs::hybrid_for.

#include "hybrid_sched/hybrid_for.hpp"

#include <cstddef>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    constexpr std::size_t N = 1 << 22; // 4M elements
    std::vector<double> a(N), b(N), c(N);
    std::iota(b.begin(), b.end(), 0.0);
    std::iota(c.begin(), c.end(), 1.0);

    // ── BEFORE: standard OpenMP ───────────────────────────────────────────────
    // #pragma omp parallel for
    // for (std::size_t i = 0; i < N; ++i)
    //     a[i] = b[i] + 3.14159 * c[i];

    // ── AFTER: hybrid_for (drop-in replacement) ───────────────────────────────
    hs::hybrid_for("example01.triad", 0, N,
                   [&](std::size_t i) { a[i] = b[i] + 3.14159 * c[i]; });

    // Verify a sample element
    const double expected = b[42] + 3.14159 * c[42];
    if (std::abs(a[42] - expected) < 1e-10)
        std::cout << "OK: a[42] = " << a[42] << "\n";
    else
        std::cout << "FAIL: expected " << expected << " got " << a[42] << "\n";

    // Print runtime info
    const hs::RuntimeInfo ri = hs::runtime_info();
    std::cout << "Topology: " << ri.topology_description << "\n";
    std::cout << "RAPL available: " << (ri.rapl_available ? "yes" : "no") << "\n";

    return 0;
}
