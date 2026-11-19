/// @file test_energy.cpp
/// Tests for RAPL delta math and graceful degradation.

#include "minitest.hpp"
#include "hybrid_sched/energy.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

TEST("energy: delta_uj no wrap") {
    const double d = hs::EnergyReader::delta_uj(100, 500, 1000000);
    CHECK_NEAR(d, 400.0, 1e-9);
}

TEST("energy: delta_uj single wrap") {
    // before=900, after=100, max_range=1000 -> delta = 100 + 1000 - 900 = 200
    const double d = hs::EnergyReader::delta_uj(900, 100, 1000);
    CHECK_NEAR(d, 200.0, 1e-9);
}

TEST("energy: delta_uj wrap with large range") {
    const std::uint64_t max = 262143328850ULL; // typical RAPL max range
    const std::uint64_t before = max - 1000;
    const std::uint64_t after = 500;
    const double d = hs::EnergyReader::delta_uj(before, after, max);
    CHECK_NEAR(d, 1500.0, 1e-3);
}

TEST("energy: EnergyReader construction does not throw") {
    hs::EnergyReader reader;
    // On non-Linux or without RAPL, available() is false but no exception
    // On Linux with permissions, available() may be true
    (void)reader.available();
}

TEST("energy: window_ok with short window") {
    hs::EnergyReader reader;
    CHECK(!reader.window_ok(0.001)); // 1 ms is too short (default 20 ms)
}

TEST("energy: window_ok with adequate window") {
    hs::EnergyReader reader;
    CHECK(reader.window_ok(0.050)); // 50 ms is fine
}

TEST("energy: read_package_uj NaN when unavailable") {
    // Create a fresh reader; on machines without RAPL it should return NaN
    hs::EnergyReader reader;
    if (!reader.available()) {
        CHECK(std::isnan(reader.read_package_uj()));
    }
    // If available, it should return a non-NaN finite value
    if (reader.available()) {
        const double v = reader.read_package_uj();
        CHECK(!std::isnan(v));
        CHECK(v >= 0.0);
    }
}
