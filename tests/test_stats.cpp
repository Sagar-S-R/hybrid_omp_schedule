/// @file test_stats.cpp
/// Tests for descriptive statistics and CI functions.

#include "minitest.hpp"
#include "hybrid_sched/stats.hpp"

#include <cmath>
#include <vector>

TEST("stats: mean empty returns NaN") {
    CHECK(std::isnan(hs::stats::mean({})));
}

TEST("stats: mean basic") {
    CHECK_NEAR(hs::stats::mean({1.0, 2.0, 3.0, 4.0, 5.0}), 3.0, 1e-10);
}

TEST("stats: stddev too few") {
    CHECK(std::isnan(hs::stats::stddev({1.0})));
}

TEST("stats: stddev known") {
    // Sample stddev (ddof=1) of {2,4,4,4,5,5,7,9} = sqrt(32/7) ≈ 2.138
    CHECK_NEAR(hs::stats::stddev({2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}), 2.13809, 1e-4);
}

TEST("stats: median odd") {
    CHECK_NEAR(hs::stats::median({3.0, 1.0, 2.0}), 2.0, 1e-10);
}

TEST("stats: median even") {
    CHECK_NEAR(hs::stats::median({1.0, 2.0, 3.0, 4.0}), 2.5, 1e-10);
}

TEST("stats: min_val / max_val") {
    const std::vector<double> v{5.0, 3.0, 8.0, 1.0};
    CHECK_NEAR(hs::stats::min_val(v), 1.0, 1e-10);
    CHECK_NEAR(hs::stats::max_val(v), 8.0, 1e-10);
}

TEST("stats: t_crit_95 df=1") {
    CHECK_NEAR(hs::stats::t_crit_95(1), 12.706, 1e-2);
}

TEST("stats: t_crit_95 df=30") {
    CHECK_NEAR(hs::stats::t_crit_95(30), 2.042, 1e-2);
}

TEST("stats: t_crit_95 df=100 uses normal approx") {
    CHECK_NEAR(hs::stats::t_crit_95(100), 1.96, 1e-2);
}

TEST("stats: ci95 width positive") {
    const std::vector<double> v{1.0, 1.1, 0.9, 1.05, 0.95, 1.02, 0.98, 1.01, 0.99, 1.0};
    const auto ci = hs::stats::ci95(v);
    CHECK(ci.half_width >= 0.0);
    CHECK(ci.lo < ci.hi);
    CHECK_GT(ci.mean_val, 0.0);
}

TEST("stats: ci95 single value has zero width") {
    const auto ci = hs::stats::ci95({5.0});
    CHECK_NEAR(ci.lo, 5.0, 1e-10);
    CHECK_NEAR(ci.hi, 5.0, 1e-10);
}

TEST("stats: bootstrap_ratio_ci symmetric data ~ 1") {
    // Ratio should be ~1.0 when num and denom are equal
    const std::vector<double> v{1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    const auto ci = hs::stats::bootstrap_ratio_ci(v, v, 123, 1000);
    CHECK_NEAR(ci.mean_val, 1.0, 1e-6);
    CHECK(ci.lo <= 1.0);
    CHECK(ci.hi >= 1.0);
}

TEST("stats: bootstrap_ratio_ci 2x ratio") {
    const std::vector<double> num{2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0};
    const std::vector<double> den{1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    const auto ci = hs::stats::bootstrap_ratio_ci(num, den, 42, 2000);
    CHECK_NEAR(ci.mean_val, 2.0, 1e-6);
    // CI should not straddle 1
    CHECK(ci.excludes_one());
}

TEST("stats: bootstrap_ratio_ci mismatched size returns NaN") {
    const auto ci = hs::stats::bootstrap_ratio_ci({1.0, 2.0}, {3.0}, 1, 100);
    CHECK(std::isnan(ci.mean_val));
}
