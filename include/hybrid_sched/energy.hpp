#pragma once
/// @file energy.hpp
/// @brief Intel RAPL energy reader with graceful degradation.
///
/// On non-Linux or when RAPL is inaccessible, all energy values are NaN and
/// available() returns false.  Callers must always check available().

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace hs {

/// Represents a single RAPL energy domain.
struct RaplDomain {
    std::string name;             ///< e.g. "package-0", "core", "uncore", "dram"
    std::string energy_path;      ///< Path to energy_uj file
    std::string max_range_path;   ///< Path to max_energy_range_uj file
    std::uint64_t last_uj{0};
    std::uint64_t max_range_uj{0};
    bool readable{false};
};

class EnergyReader {
public:
    EnergyReader();

    /// True if the package-0 domain is readable.
    [[nodiscard]] bool available() const noexcept { return available_; }

    /// Read current package energy in microjoules.
    /// Returns NaN if not available.
    [[nodiscard]] double read_package_uj();

    /// Compute delta (handles single counter wrap) from two raw microjoule readings.
    [[nodiscard]] static double delta_uj(std::uint64_t before, std::uint64_t after,
                                          std::uint64_t max_range) noexcept;

    /// True if the elapsed time is long enough for a trustworthy RAPL reading.
    [[nodiscard]] bool window_ok(double elapsed_seconds) const noexcept;

    /// Sample idle power over duration_s seconds (blocks, uses steady_clock).
    /// Returns NaN if not available or duration is too short.
    [[nodiscard]] double measure_idle_power_w(double duration_s = 5.0);

    /// Names of all discovered domains (for diagnostics).
    [[nodiscard]] const std::vector<RaplDomain>& domains() const noexcept { return domains_; }

    /// Minimum window for a trustworthy measurement (configurable via HS_RAPL_MIN_MS, default 20 ms).
    [[nodiscard]] double min_window_s() const noexcept { return min_window_s_; }

private:
    void discover_domains();

    std::vector<RaplDomain> domains_;
    int package_idx_{-1};  ///< Index into domains_ for package-0
    bool available_{false};
    double min_window_s_{0.020};  ///< Default 20 ms
};

} // namespace hs
