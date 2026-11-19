/// @file energy.cpp

#include "hybrid_sched/energy.hpp"
#include "hybrid_sched/timer.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <dirent.h>
#endif

namespace hs {

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::uint64_t read_uj(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return 0;
    std::uint64_t val = 0;
    f >> val;
    return val;
}

// ── EnergyReader implementation ───────────────────────────────────────────────

EnergyReader::EnergyReader() {
    const double env_min_ms = util::getenv_double("HS_RAPL_MIN_MS", 20.0);
    min_window_s_ = env_min_ms * 1e-3;

    discover_domains();
}

void EnergyReader::discover_domains() {
#if defined(__linux__)
    // Scan /sys/class/powercap/ for intel-rapl:* entries
    const std::string base = "/sys/class/powercap/";

    DIR* dir = opendir(base.c_str());
    if (!dir) {
        util::warn_once(
            "RAPL not available: cannot open /sys/class/powercap/. "
            "Energy measurements will be NaN. Run scripts/rapl_permissions.sh.");
        return;
    }

    std::vector<std::string> entries;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const std::string name(ent->d_name);
        if (name.substr(0, 11) == "intel-rapl:") entries.push_back(name);
    }
    closedir(dir);

    std::sort(entries.begin(), entries.end());

    for (const auto& entry : entries) {
        const std::string dir_path = base + entry + "/";
        const std::string name_str = util::read_first_line(dir_path + "name");
        const std::string energy_path = dir_path + "energy_uj";
        const std::string max_path = dir_path + "max_energy_range_uj";

        // Try reading energy_uj
        std::ifstream test(energy_path);
        if (!test.is_open()) continue;

        RaplDomain dom;
        dom.name = name_str.empty() ? entry : name_str;
        dom.energy_path = energy_path;
        dom.max_range_path = max_path;
        dom.readable = true;

        // Read max range
        const std::string max_str = util::read_first_line(max_path);
        if (!max_str.empty()) {
            try {
                dom.max_range_uj = std::stoull(max_str);
            } catch (...) {
                dom.max_range_uj = 0;
            }
        }

        util::log("RAPL domain found: " + dom.name + " (" + entry + ")");

        if (dom.name == "package-0" && package_idx_ < 0) {
            package_idx_ = static_cast<int>(domains_.size());
            available_ = true;
        }
        domains_.push_back(std::move(dom));
    }

    if (!available_) {
        util::warn_once(
            "RAPL package-0 domain not found or not readable. "
            "Energy measurements will be NaN. "
            "Try: sudo scripts/rapl_permissions.sh or run benchmarks with sudo.");
    }
#else
    util::log("RAPL not available on this platform (non-Linux).", false);
#endif
}

double EnergyReader::read_package_uj() {
    if (!available_ || package_idx_ < 0)
        return std::numeric_limits<double>::quiet_NaN();

    const auto val = read_uj(domains_[static_cast<std::size_t>(package_idx_)].energy_path);
    return static_cast<double>(val);
}

double EnergyReader::delta_uj(std::uint64_t before, std::uint64_t after,
                               std::uint64_t max_range) noexcept {
    if (after >= before) {
        return static_cast<double>(after - before);
    }
    // Single wrap
    if (max_range > 0) {
        return static_cast<double>(after + max_range - before);
    }
    // Unknown max range: assume 32-bit counter
    constexpr std::uint64_t default_max = (1ULL << 32);
    return static_cast<double>(after + default_max - before);
}

bool EnergyReader::window_ok(double elapsed_seconds) const noexcept {
    return elapsed_seconds >= min_window_s_;
}

double EnergyReader::measure_idle_power_w(double duration_s) {
    if (!available_) return std::numeric_limits<double>::quiet_NaN();
    if (duration_s < min_window_s_ * 2.0) return std::numeric_limits<double>::quiet_NaN();

    const double uj_before = read_package_uj();
    if (std::isnan(uj_before)) return std::numeric_limits<double>::quiet_NaN();

    const auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(
        std::chrono::duration<double>(duration_s));
    const auto t1 = std::chrono::steady_clock::now();

    const double uj_after = read_package_uj();
    if (std::isnan(uj_after)) return std::numeric_limits<double>::quiet_NaN();

    const double elapsed_s =
        std::chrono::duration<double>(t1 - t0).count();

    if (!window_ok(elapsed_s)) return std::numeric_limits<double>::quiet_NaN();

    const std::uint64_t max_range =
        domains_[static_cast<std::size_t>(package_idx_)].max_range_uj;
    const double delta = delta_uj(static_cast<std::uint64_t>(uj_before),
                                   static_cast<std::uint64_t>(uj_after), max_range);
    return (delta * 1e-6) / elapsed_s; // W
}

} // namespace hs
