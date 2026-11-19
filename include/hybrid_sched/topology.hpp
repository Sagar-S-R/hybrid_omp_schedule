#pragma once
/// @file topology.hpp
/// @brief CPU topology detection (P-core / E-core), SMT siblings, thread pinning.

#include <string>
#include <vector>

namespace hs {

/// Core type classification.
enum class CoreType { P, E, Unknown };

/// Per-logical-CPU descriptor.
struct CpuInfo {
    int id;           ///< Linux CPU number (0-based)
    CoreType type;
    int core_id;      ///< Physical core id within the socket
    int sibling_rank; ///< 0 = primary thread, 1 = SMT sibling, -1 = unknown
    long max_khz;     ///< Maximum frequency from cpufreq (0 if unknown)
};

/// Full topology snapshot.
struct Topology {
    std::vector<CpuInfo> cpus;       ///< All logical CPUs, sorted by id
    std::vector<int> p_cpus;         ///< P-core logical CPU ids (primary SMT first)
    std::vector<int> e_cpus;         ///< E-core logical CPU ids
    int n_p_phys{0};                 ///< Number of distinct P physical cores
    bool smt_active{false};          ///< True if any SMT siblings detected
    bool hybrid{false};              ///< True if both P and E cores present
    bool detection_reliable{false};  ///< False if only the frequency heuristic was used

    /// Number of P slots (primary + SMT sibling logical CPUs).
    [[nodiscard]] int n_p_slots() const noexcept {
        return static_cast<int>(p_cpus.size());
    }
    /// Number of E slots.
    [[nodiscard]] int n_e_slots() const noexcept {
        return static_cast<int>(e_cpus.size());
    }
    /// Total slots.
    [[nodiscard]] int n_slots() const noexcept {
        return n_p_slots() + n_e_slots();
    }
};

// ── Pure parsing helpers (usable in unit tests with injected strings) ─────────

/// Parse a cpulist string (e.g. "0-3,8,10-11") into a sorted vector of CPU ids.
/// Returns empty on error.
[[nodiscard]] std::vector<int> parse_cpulist(const std::string& s);

/// Build a CpuInfo list from fake/injected sysfs strings for testing.
/// p_cpulist / e_cpulist: cpulist strings. thread_siblings_map: map "cpu_id -> sibling_cpu_id"
/// expressed as a string "0:1,2:3" (pairs of siblings, 0 if none).
[[nodiscard]] std::vector<CpuInfo> build_cpuinfo_from_strings(const std::string& p_cpulist,
                                                               const std::string& e_cpulist,
                                                               const std::string& thread_siblings_map,
                                                               const std::string& max_khz_map);

// ── Runtime detection ─────────────────────────────────────────────────────────

/// Detect the system topology.  Checks env overrides first, then sysfs, then heuristics.
/// Thread-safe (uses a process-wide singleton after first call).
[[nodiscard]] Topology detect_topology();

/// Returns true if the detected topology has both P and E cores.
[[nodiscard]] bool is_hybrid();

/// Human-readable description of the topology (for diagnostics).
[[nodiscard]] std::string describe_topology(const Topology& topo);

// ── Pinning ───────────────────────────────────────────────────────────────────

/// Pin the calling thread to a specific logical CPU.
/// Returns true on success, false if pinning is not supported or disabled.
/// Warns once on first failure.
bool pin_current_thread(int cpu_id) noexcept;

/// Unpin the calling thread (allow it to run on any CPU in the affinity mask).
bool unpin_current_thread() noexcept;

} // namespace hs
