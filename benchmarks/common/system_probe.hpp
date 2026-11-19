#pragma once
/// @file system_probe.hpp
/// @brief Read system state: temperature, frequency, governor, turbo, idle power.

#include <string>

namespace hs {
namespace bench {

struct SystemState {
    double pkg_temp_c{-1.0};    ///< x86_pkg_temp in °C, -1 if unavailable
    long p_core_khz{0};         ///< Current frequency of first P-core
    long e_core_khz{0};         ///< Current frequency of first E-core (0 if no E)
    std::string governor;       ///< e.g. "performance", "powersave"
    bool turbo_enabled{true};   ///< false if no_turbo=1
    bool intel_pstate{false};   ///< true if intel_pstate driver active
};

/// Read current system state (non-blocking, best-effort).
[[nodiscard]] SystemState probe_system();

/// Read package temperature in °C from thermal_zone (x86_pkg_temp), or -1.
[[nodiscard]] double read_pkg_temp_c();

/// Read current scaling frequency of a CPU in kHz, or 0.
[[nodiscard]] long read_cpu_cur_khz(int cpu_id);

/// Read CPU frequency governor for cpu 0.
[[nodiscard]] std::string read_governor();

/// True if intel_pstate no_turbo is active (turbo disabled).
[[nodiscard]] bool turbo_disabled();

} // namespace bench
} // namespace hs
