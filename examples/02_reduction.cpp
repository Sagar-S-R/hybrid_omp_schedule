/// @file 02_reduction.cpp
/// Demonstrates hs::hybrid_reduce for a parallel sum.

#include "hybrid_sched/hybrid_for.hpp"

#include <cstddef>
#include <iostream>
#include <numeric>

int main() {
    constexpr std::size_t N = 1 << 20;

    // Sum of i*i for i in [0, N) — compare parallel vs serial
    const long serial_result = [N]() {
        long s = 0;
        for (std::size_t i = 0; i < N; ++i) s += static_cast<long>(i);
        return s;
    }();

    const long parallel_result = hs::hybrid_reduce<long>(
        "example02.sum",
        0, N,
        0L,
        [](std::size_t i, long& acc) { acc += static_cast<long>(i); },
        [](long a, long b) { return a + b; });

    if (serial_result == parallel_result)
        std::cout << "OK: sum = " << parallel_result << "\n";
    else
        std::cout << "FAIL: serial=" << serial_result
                  << " parallel=" << parallel_result << "\n";

    return 0;
}
