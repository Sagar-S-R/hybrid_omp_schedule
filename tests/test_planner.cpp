/// @file test_planner.cpp
/// Tests for the planner: hand-computed EDP cases, policy enforcement.

#include "minitest.hpp"
#include "hybrid_sched/planner.hpp"
#include "hybrid_sched/power_model.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

// Helper: build a PlannerInput with a fixed power profile string
static hs::PlannerInput make_input(const std::vector<double>& speeds,
                                    int nP, int nE, int nP_phys,
                                    std::size_t N, hs::Policy policy = hs::Policy::EDP) {
    static hs::PowerModel pm; // uses default profile
    hs::PlannerInput pi;
    pi.slot_speeds = &speeds;
    pi.n_p_slots = nP;
    pi.n_e_slots = nE;
    pi.n_p_phys = nP_phys;
    pi.n_remaining = N;
    pi.kappa = 1.0;
    pi.fork_join_by_threads = nullptr;
    pi.policy = policy;
    pi.min_parallel_seconds = 0.0; // no serial threshold in tests
    pi.power_model = &pm;
    return pi;
}

TEST("planner: P_ONLY selects only P slots") {
    const std::vector<double> speeds{2.0, 1.8, 1.0, 1.0}; // 2P + 2E
    auto pi = make_input(speeds, 2, 2, 2, 10000, hs::Policy::P_ONLY);
    const auto plan = hs::choose_plan(pi);
    CHECK_EQ(plan.a, 2);
    CHECK_EQ(plan.b, 0);
    CHECK(!plan.run_serial);
}

TEST("planner: E_ONLY selects only E slots") {
    const std::vector<double> speeds{2.0, 1.0, 1.0}; // 1P + 2E
    auto pi = make_input(speeds, 1, 2, 1, 10000, hs::Policy::E_ONLY);
    const auto plan = hs::choose_plan(pi);
    CHECK_EQ(plan.a, 0);
    CHECK_EQ(plan.b, 2);
    CHECK(!plan.run_serial);
}

TEST("planner: TIME_BALANCED uses all slots") {
    const std::vector<double> speeds{3.0, 2.0, 1.0, 1.0}; // 2P + 2E
    auto pi = make_input(speeds, 2, 2, 2, 10000, hs::Policy::TIME_BALANCED);
    const auto plan = hs::choose_plan(pi);
    CHECK_EQ(plan.a, 2);
    CHECK_EQ(plan.b, 2);
}

TEST("planner: weights sum to 1 for EDP policy") {
    const std::vector<double> speeds{4.0, 3.0, 1.0, 1.0, 1.0, 1.0}; // 2P + 4E
    auto pi = make_input(speeds, 2, 4, 2, 100000, hs::Policy::EDP);
    const auto plan = hs::choose_plan(pi);
    if (!plan.run_serial) {
        double w_sum = 0.0;
        for (double w : plan.weights) w_sum += w;
        CHECK_NEAR(w_sum, 1.0, 1e-9);
    }
}

TEST("planner: EDP selects a valid a+b combination") {
    const std::vector<double> speeds{5.0, 4.0, 1.5, 1.5, 1.5, 1.5}; // 2P + 4E
    auto pi = make_input(speeds, 2, 4, 2, 500000, hs::Policy::EDP);
    const auto plan = hs::choose_plan(pi);
    CHECK(!plan.run_serial);
    CHECK(plan.a >= 0 && plan.a <= 2);
    CHECK(plan.b >= 0 && plan.b <= 4);
    CHECK(plan.a + plan.b >= 1);
}

TEST("planner: enumerate_candidates count") {
    // nP=2, nE=3: (2+1)*(3+1)-1 = 11 candidates
    const std::vector<double> speeds{2.0, 1.8, 1.0, 1.0, 1.0}; // 2P + 3E
    auto pi = make_input(speeds, 2, 3, 2, 10000);
    const auto cands = hs::enumerate_candidates(pi);
    CHECK_EQ(cands.size(), std::size_t{11});
}

TEST("planner: FIXED_RATIO uses all slots") {
    const std::vector<double> speeds{2.0, 1.0, 1.0}; // 1P + 2E
    auto pi = make_input(speeds, 1, 2, 1, 10000, hs::Policy::FIXED_RATIO);
    pi.fixed_ratio_p_over_e = 2.0;
    const auto plan = hs::choose_plan(pi);
    CHECK_EQ(plan.a, 1);
    CHECK_EQ(plan.b, 2);
}

TEST("planner: EDP predicted values are positive") {
    const std::vector<double> speeds{3.0, 2.5, 1.2, 1.1, 1.0, 1.0, 1.0, 1.0}; // 2P(4log) + 4E
    auto pi = make_input(speeds, 4, 4, 2, 1000000, hs::Policy::EDP);
    const auto plan = hs::choose_plan(pi);
    if (!plan.run_serial) {
        CHECK_GT(plan.pred_time_s, 0.0);
        CHECK_GT(plan.pred_power_w, 0.0);
        CHECK_GT(plan.pred_energy_j, 0.0);
        CHECK_GT(plan.pred_edp, 0.0);
    }
}

TEST("planner: homogeneous P-only topology works") {
    // 4 identical P cores, no E
    const std::vector<double> speeds{2.0, 2.0, 2.0, 2.0};
    auto pi = make_input(speeds, 4, 0, 4, 100000, hs::Policy::EDP);
    const auto plan = hs::choose_plan(pi);
    CHECK_EQ(plan.b, 0);
    CHECK(plan.a >= 1 && plan.a <= 4);
    CHECK(!plan.run_serial);
}
