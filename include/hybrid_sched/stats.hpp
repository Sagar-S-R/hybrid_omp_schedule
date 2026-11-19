#pragma once
/// @file stats.hpp
/// @brief Descriptive statistics, Student's t confidence intervals, and bootstrap ratio CIs.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace hs {
namespace stats {

// ── Basic descriptors ────────────────────────────────────────────────────────

/// Arithmetic mean.  Returns NaN if v is empty.
[[nodiscard]] inline double mean(const std::vector<double>& v) {
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

/// Sample standard deviation (ddof=1).  Returns NaN if n < 2.
[[nodiscard]] inline double stddev(const std::vector<double>& v) {
    if (v.size() < 2) return std::numeric_limits<double>::quiet_NaN();
    const double m = mean(v);
    double acc = 0.0;
    for (double x : v) acc += (x - m) * (x - m);
    return std::sqrt(acc / static_cast<double>(v.size() - 1));
}

/// Median (copies and sorts).  Returns NaN if empty.
[[nodiscard]] inline double median(std::vector<double> v) {
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    const std::size_t n = v.size();
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
    if (n % 2 == 1) return v[n / 2];
    // Even: average of two middle elements
    const double upper = v[n / 2];
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n / 2 - 1), v.end());
    return (v[n / 2 - 1] + upper) * 0.5;
}

/// Minimum.  Returns NaN if empty.
[[nodiscard]] inline double min_val(const std::vector<double>& v) {
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    return *std::min_element(v.begin(), v.end());
}

/// Maximum.  Returns NaN if empty.
[[nodiscard]] inline double max_val(const std::vector<double>& v) {
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    return *std::max_element(v.begin(), v.end());
}

// ── t-distribution quantile (two-tailed, 95% CI = alpha 0.05) ───────────────
//
// Embedded table for df 1..30; normal approximation (1.96) beyond.

/// Return the t critical value for a two-tailed 95% CI at the given degrees of freedom.
[[nodiscard]] inline double t_crit_95(int df) {
    // Table: t_{0.025} for df = 1..30
    static constexpr double kTable[30] = {
        12.706, 4.303, 3.182, 2.776, 2.571,  // df 1-5
        2.447,  2.365, 2.306, 2.262, 2.228,  // df 6-10
        2.201,  2.179, 2.160, 2.145, 2.131,  // df 11-15
        2.120,  2.110, 2.101, 2.093, 2.086,  // df 16-20
        2.080,  2.074, 2.069, 2.064, 2.060,  // df 21-25
        2.056,  2.052, 2.048, 2.045, 2.042   // df 26-30
    };
    if (df < 1) return std::numeric_limits<double>::quiet_NaN();
    if (df <= 30) return kTable[df - 1];
    return 1.96; // normal approximation for df > 30
}

// ── Confidence interval ──────────────────────────────────────────────────────

struct CI {
    double lo;   ///< lower bound
    double hi;   ///< upper bound
    double mean_val;
    double half_width;
    int n;

    /// True when the interval does NOT straddle 1.0 (useful for ratios).
    [[nodiscard]] bool excludes_one() const noexcept { return lo > 1.0 || hi < 1.0; }
};

/// 95% confidence interval using Student's t.  Requires n >= 2.
[[nodiscard]] inline CI ci95(const std::vector<double>& v) {
    CI c{};
    c.n = static_cast<int>(v.size());
    c.mean_val = mean(v);
    if (c.n < 2) {
        c.lo = c.hi = c.mean_val;
        c.half_width = 0.0;
        return c;
    }
    const double s = stddev(v);
    const double t = t_crit_95(c.n - 1);
    c.half_width = t * s / std::sqrt(static_cast<double>(c.n));
    c.lo = c.mean_val - c.half_width;
    c.hi = c.mean_val + c.half_width;
    return c;
}

// ── Bootstrap ratio CI ───────────────────────────────────────────────────────

/// Compute 95% CI for the ratio mean(num)/mean(denom) using the bootstrap.
/// @param num       numerator samples
/// @param denom     denominator samples (same length as num, paired)
/// @param seed      RNG seed for reproducibility
/// @param B         number of bootstrap resamples (default 10 000)
[[nodiscard]] inline CI bootstrap_ratio_ci(const std::vector<double>& num,
                                            const std::vector<double>& denom,
                                            std::uint64_t seed = 42, int B = 10000) {
    const int n = static_cast<int>(num.size());
    if (n == 0 || n != static_cast<int>(denom.size())) {
        return CI{std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::quiet_NaN(),
                  0};
    }

    // splitmix64 RNG (no stdlib rand())
    auto sm64 = [&seed]() -> std::uint64_t {
        seed += 0x9e3779b97f4a7c15ULL;
        std::uint64_t z = seed;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    };

    std::vector<double> ratios(static_cast<std::size_t>(B));
    for (int b = 0; b < B; ++b) {
        double sn = 0.0, sd = 0.0;
        for (int i = 0; i < n; ++i) {
            int idx = static_cast<int>(sm64() % static_cast<std::uint64_t>(n));
            sn += num[static_cast<std::size_t>(idx)];
            sd += denom[static_cast<std::size_t>(idx)];
        }
        ratios[static_cast<std::size_t>(b)] = (sd != 0.0) ? (sn / static_cast<double>(n)) /
                                                                 (sd / static_cast<double>(n))
                                                           : std::numeric_limits<double>::quiet_NaN();
    }

    std::sort(ratios.begin(), ratios.end());
    const int lo_idx = static_cast<int>(0.025 * B);
    const int hi_idx = static_cast<int>(0.975 * B);
    const double point_est = (mean(num) != 0.0 && mean(denom) != 0.0)
                                 ? mean(num) / mean(denom)
                                 : std::numeric_limits<double>::quiet_NaN();
    CI c{};
    c.lo = ratios[static_cast<std::size_t>(lo_idx)];
    c.hi = ratios[static_cast<std::size_t>(hi_idx)];
    c.mean_val = point_est;
    c.half_width = (c.hi - c.lo) * 0.5;
    c.n = n;
    return c;
}

} // namespace stats
} // namespace hs
