/// @file test_topology.cpp
/// Tests for topology detection using pure-function string injection.

#include "minitest.hpp"
#include "hybrid_sched/topology.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

TEST("topology: build_cpuinfo basic P+E") {
    // 2 P physical cores (0,1), 4 E cores (4-7), no SMT, no freq info
    const auto cpus = hs::build_cpuinfo_from_strings("0-1", "4-7", "", "");
    CHECK_EQ(cpus.size(), std::size_t{6});

    int n_p = 0, n_e = 0;
    for (const auto& c : cpus) {
        if (c.type == hs::CoreType::P) ++n_p;
        if (c.type == hs::CoreType::E) ++n_e;
    }
    CHECK_EQ(n_p, 2);
    CHECK_EQ(n_e, 4);
}

TEST("topology: build_cpuinfo SMT siblings") {
    // P cores 0 and 1 are SMT siblings; P cores 2 and 3 are siblings
    const auto cpus = hs::build_cpuinfo_from_strings("0-3", "8-11", "0:1,2:3", "");
    // Find CPU 0 and CPU 1 — they should be siblings
    int rank0 = -1, rank1 = -1;
    for (const auto& c : cpus) {
        if (c.id == 0) rank0 = c.sibling_rank;
        if (c.id == 1) rank1 = c.sibling_rank;
    }
    CHECK_EQ(rank0, 0);  // lower id = primary
    CHECK_EQ(rank1, 1);  // higher id = sibling
}

TEST("topology: parse_cpulist basic") {
    const auto v = hs::parse_cpulist("0-3,8,10-11");
    CHECK_EQ(v.size(), std::size_t{7});
    CHECK(std::find(v.begin(), v.end(), 8) != v.end());
}

TEST("topology: build_cpuinfo P-only (homogeneous)") {
    const auto cpus = hs::build_cpuinfo_from_strings("0-3", "", "", "");
    for (const auto& c : cpus) {
        CHECK(c.type == hs::CoreType::P);
    }
    CHECK_EQ(cpus.size(), std::size_t{4});
}

TEST("topology: build_cpuinfo E-only") {
    const auto cpus = hs::build_cpuinfo_from_strings("", "4-7", "", "");
    for (const auto& c : cpus) {
        CHECK(c.type == hs::CoreType::E);
    }
    CHECK_EQ(cpus.size(), std::size_t{4});
}

TEST("topology: detect_topology does not crash") {
    // Just verify it returns without throwing
    const hs::Topology topo = hs::detect_topology();
    CHECK(topo.n_slots() >= 1);
    CHECK(!topo.p_cpus.empty() || !topo.e_cpus.empty());
}

TEST("topology: describe_topology non-empty") {
    const hs::Topology topo = hs::detect_topology();
    const std::string desc = hs::describe_topology(topo);
    CHECK(!desc.empty());
}

TEST("topology: HS_FORCE_HOMOGENEOUS merges all into P") {
    // This test only checks that detect_topology() succeeds with the env set
    // (the env was set by CI; on the native machine it may not be set)
    // We verify the invariant that n_e_slots == 0 when forced homogeneous
    // by directly calling detect_topology() — the singleton may have already
    // been initialised without the env, so we just verify it doesn't crash.
    const hs::Topology topo = hs::detect_topology();
    const char* env = std::getenv("HS_FORCE_HOMOGENEOUS");
    if (env && (env[0] == '1')) {
        CHECK_EQ(topo.n_e_slots(), 0);
    }
    CHECK(topo.n_slots() >= 1);
}
