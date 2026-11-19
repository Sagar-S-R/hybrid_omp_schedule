/// @file topology.cpp

#include "hybrid_sched/topology.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

// <thread> already included above for hardware_concurrency

namespace hs {

// ── cpulist parsing (also exported from util, but topology needs its own for purity) ──

std::vector<int> parse_cpulist(const std::string& s) {
    return util::parse_cpulist(s);
}

// ── build_cpuinfo_from_strings ────────────────────────────────────────────────
// Pure function for unit testing: builds a CpuInfo list from injected strings.

std::vector<CpuInfo> build_cpuinfo_from_strings(const std::string& p_cpulist,
                                                  const std::string& e_cpulist,
                                                  const std::string& thread_siblings_map,
                                                  const std::string& max_khz_map) {
    const std::vector<int> p_ids = parse_cpulist(p_cpulist);
    const std::vector<int> e_ids = parse_cpulist(e_cpulist);

    // Parse sibling pairs: "0:1,2:3" -> map cpu->sibling
    std::map<int, int> sibling_of;
    if (!thread_siblings_map.empty()) {
        for (const auto& pair_str : util::split(thread_siblings_map, ',')) {
            const auto parts = util::split(pair_str, ':');
            if (parts.size() == 2) {
                try {
                    const int a = std::stoi(util::trim(parts[0]));
                    const int b = std::stoi(util::trim(parts[1]));
                    sibling_of[a] = b;
                    sibling_of[b] = a;
                } catch (...) {}
            }
        }
    }

    // Parse max_khz: "0:3200000,1:3200000,..." -> map cpu->khz
    std::map<int, long> khz_of;
    if (!max_khz_map.empty()) {
        for (const auto& entry : util::split(max_khz_map, ',')) {
            const auto parts = util::split(entry, ':');
            if (parts.size() == 2) {
                try {
                    const int cpu = std::stoi(util::trim(parts[0]));
                    const long khz = std::stol(util::trim(parts[1]));
                    khz_of[cpu] = khz;
                } catch (...) {}
            }
        }
    }

    std::vector<CpuInfo> result;

    auto add_cpus = [&](const std::vector<int>& ids, CoreType type) {
        // Determine physical cores by grouping siblings
        std::map<int, int> cpu_to_core;
        int next_core = (type == CoreType::P) ? 0 : 1000; // separate ranges
        std::set<int> assigned;
        for (int cpu : ids) {
            if (assigned.count(cpu)) continue;
            cpu_to_core[cpu] = next_core;
            assigned.insert(cpu);
            if (sibling_of.count(cpu)) {
                const int sib = sibling_of.at(cpu);
                cpu_to_core[sib] = next_core;
                assigned.insert(sib);
            }
            ++next_core;
        }
        for (int cpu : ids) {
            CpuInfo info;
            info.id = cpu;
            info.type = type;
            info.core_id = cpu_to_core.count(cpu) ? cpu_to_core.at(cpu) : cpu;
            info.max_khz = khz_of.count(cpu) ? khz_of.at(cpu) : 0;
            // sibling_rank: 0 if first in a pair, 1 if second
            if (sibling_of.count(cpu)) {
                const int sib = sibling_of.at(cpu);
                info.sibling_rank = (cpu < sib) ? 0 : 1;
            } else {
                info.sibling_rank = 0;
            }
            result.push_back(info);
        }
    };

    add_cpus(p_ids, CoreType::P);
    add_cpus(e_ids, CoreType::E);

    std::sort(result.begin(), result.end(), [](const CpuInfo& a, const CpuInfo& b) {
        return a.id < b.id;
    });
    return result;
}

// ── Runtime detection helpers (Linux-specific) ────────────────────────────────

#if defined(__linux__)
static std::vector<int> read_sysfs_cpulist(const std::string& path) {
    const std::string content = util::read_first_line(path);
    if (content.empty()) return {};
    return parse_cpulist(content);
}

static long read_cpu_max_freq_khz(int cpu) {
    const std::string path =
        "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/cpuinfo_max_freq";
    const std::string val = util::read_first_line(path);
    if (val.empty()) return 0;
    try {
        return std::stol(val);
    } catch (...) {
        return 0;
    }
}

static int read_core_id(int cpu) {
    const std::string path =
        "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_id";
    const std::string val = util::read_first_line(path);
    if (val.empty()) return cpu;
    try {
        return std::stoi(val);
    } catch (...) {
        return cpu;
    }
}

static std::vector<int> read_thread_siblings(int cpu) {
    const std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) +
                              "/topology/thread_siblings_list";
    const std::string content = util::read_first_line(path);
    if (content.empty()) return {cpu};
    auto ids = parse_cpulist(content);
    if (ids.empty()) return {cpu};
    return ids;
}
#endif // __linux__

// ── detect_topology implementation ───────────────────────────────────────────

static Topology do_detect() {
    Topology topo;

    // ── 1. Env overrides ──────────────────────────────────────────────────────
    const bool force_homogeneous = util::getenv_bool("HS_FORCE_HOMOGENEOUS");
    const std::string env_p = util::getenv_str("HS_P_CPUS");
    const std::string env_e = util::getenv_str("HS_E_CPUS");
    const bool disable_pinning = util::getenv_bool("HS_DISABLE_PINNING");
    (void)disable_pinning;

    std::vector<int> p_ids, e_ids;
    bool env_override = false;

    if (!env_p.empty() || !env_e.empty()) {
        p_ids = parse_cpulist(env_p);
        e_ids = parse_cpulist(env_e);
        env_override = true;
        topo.detection_reliable = true;
        util::log("Topology from env: P=" + env_p + " E=" + env_e);
    }

#if defined(__linux__)
    // ── 2. Intel hybrid sysfs ─────────────────────────────────────────────────
    if (!env_override && !force_homogeneous) {
        const std::vector<int> sysfs_p = read_sysfs_cpulist("/sys/devices/cpu_core/cpus");
        const std::vector<int> sysfs_e = read_sysfs_cpulist("/sys/devices/cpu_atom/cpus");
        if (!sysfs_p.empty() || !sysfs_e.empty()) {
            p_ids = sysfs_p;
            e_ids = sysfs_e;
            topo.detection_reliable = true;
            util::log("Topology from Intel hybrid sysfs: P=" + util::format_cpulist(p_ids) +
                      " E=" + util::format_cpulist(e_ids));
        }
    }

    // ── 3. Frequency heuristic fallback ───────────────────────────────────────
    if (!env_override && !force_homogeneous && p_ids.empty() && e_ids.empty()) {
        const int n = (int)std::thread::hardware_concurrency();
        long max_freq = 0;
        std::vector<long> freqs(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            freqs[static_cast<std::size_t>(i)] = read_cpu_max_freq_khz(i);
            if (freqs[static_cast<std::size_t>(i)] > max_freq)
                max_freq = freqs[static_cast<std::size_t>(i)];
        }
        if (max_freq > 0) {
            const long threshold = static_cast<long>(max_freq * 0.85);
            for (int i = 0; i < n; ++i) {
                if (freqs[static_cast<std::size_t>(i)] < threshold && freqs[static_cast<std::size_t>(i)] > 0)
                    e_ids.push_back(i);
                else
                    p_ids.push_back(i);
            }
            if (!e_ids.empty()) {
                topo.detection_reliable = false;
                util::log("Topology from frequency heuristic (not reliable)");
            } else {
                p_ids.clear(); // all same freq -> homogeneous below
            }
        }
    }
#endif // __linux__

    // ── 4. Homogeneous fallback ───────────────────────────────────────────────
    if (p_ids.empty() && e_ids.empty()) {
        const int n = static_cast<int>(std::thread::hardware_concurrency());
        for (int i = 0; i < n; ++i) p_ids.push_back(i);
        topo.detection_reliable = false;
        util::log("Topology: homogeneous fallback (" + std::to_string(n) + " CPUs)");
    }

    // If force homogeneous, merge all into p
    if (force_homogeneous) {
        for (int id : e_ids) p_ids.push_back(id);
        e_ids.clear();
        std::sort(p_ids.begin(), p_ids.end());
    }

    topo.hybrid = (!p_ids.empty() && !e_ids.empty());

    // ── Build CpuInfo list ────────────────────────────────────────────────────

#if defined(__linux__)
    // Build sibling map
    std::map<int, int> sibling_of;
    auto all_ids = p_ids;
    all_ids.insert(all_ids.end(), e_ids.begin(), e_ids.end());
    for (int cpu : all_ids) {
        const auto siblings = read_thread_siblings(cpu);
        if (siblings.size() >= 2) {
            for (std::size_t si = 0; si < siblings.size(); ++si) {
                if (siblings[si] != cpu) {
                    // Only record if the sibling is in our known set
                    sibling_of[cpu] = siblings[si];
                    break;
                }
            }
        }
    }

    auto make_cpu_info = [&](int cpu, CoreType type) {
        CpuInfo info;
        info.id = cpu;
        info.type = type;
        info.core_id = read_core_id(cpu);
        info.max_khz = read_cpu_max_freq_khz(cpu);
        if (sibling_of.count(cpu)) {
            const int sib = sibling_of.at(cpu);
            info.sibling_rank = (cpu < sib) ? 0 : 1;
        } else {
            info.sibling_rank = 0;
        }
        return info;
    };
#else
    auto make_cpu_info = [&](int cpu, CoreType type) {
        CpuInfo info;
        info.id = cpu;
        info.type = type;
        info.core_id = cpu;
        info.max_khz = 0;
        info.sibling_rank = 0;
        return info;
    };
    std::map<int, int> sibling_of;
#endif

    for (int cpu : p_ids) topo.cpus.push_back(make_cpu_info(cpu, CoreType::P));
    for (int cpu : e_ids) topo.cpus.push_back(make_cpu_info(cpu, CoreType::E));
    std::sort(topo.cpus.begin(), topo.cpus.end(),
              [](const CpuInfo& a, const CpuInfo& b) { return a.id < b.id; });

    // ── Build p_cpus ordered: primary threads first, then SMT siblings ────────
    // Primary = sibling_rank 0 (or unknown)
    for (const auto& ci : topo.cpus) {
        if (ci.type == CoreType::P && ci.sibling_rank <= 0) topo.p_cpus.push_back(ci.id);
    }
    // SMT siblings
    for (const auto& ci : topo.cpus) {
        if (ci.type == CoreType::P && ci.sibling_rank > 0) topo.p_cpus.push_back(ci.id);
    }
    // E cores
    for (const auto& ci : topo.cpus) {
        if (ci.type == CoreType::E) topo.e_cpus.push_back(ci.id);
    }

    // Count P physical cores (unique core_id among P CPUs)
    {
        std::vector<int> phys_ids;
        for (const auto& ci : topo.cpus) {
            if (ci.type == CoreType::P && ci.sibling_rank <= 0) phys_ids.push_back(ci.core_id);
        }
        std::sort(phys_ids.begin(), phys_ids.end());
        phys_ids.erase(std::unique(phys_ids.begin(), phys_ids.end()), phys_ids.end());
        topo.n_p_phys = static_cast<int>(phys_ids.size());
        if (topo.n_p_phys == 0 && !p_ids.empty())
            topo.n_p_phys = static_cast<int>(p_ids.size()); // fallback
    }

    // SMT active if any P CPU has sibling_rank > 0
    for (const auto& ci : topo.cpus) {
        if (ci.type == CoreType::P && ci.sibling_rank > 0) {
            topo.smt_active = true;
            break;
        }
    }

    // Warn if OMP_PROC_BIND is set to something that fights our pinning
    const std::string proc_bind = util::getenv_str("OMP_PROC_BIND");
    if (!proc_bind.empty() && proc_bind != "false" && proc_bind != "FALSE" && proc_bind != "0") {
        util::warn_once("OMP_PROC_BIND=" + proc_bind +
                        " may conflict with hybrid_sched pinning.  "
                        "Set OMP_PROC_BIND=false for best results.");
    }

    return topo;
}

// Global singleton
static Topology g_topology;
static std::once_flag g_topo_flag;

Topology detect_topology() {
    std::call_once(g_topo_flag, []() { g_topology = do_detect(); });
    return g_topology;
}

bool is_hybrid() {
    return detect_topology().hybrid;
}

std::string describe_topology(const Topology& t) {
    std::string s;
    s += "CPUs: " + std::to_string(t.n_slots());
    s += "  P-slots: " + std::to_string(t.n_p_slots());
    s += " (phys=" + std::to_string(t.n_p_phys) + ")";
    s += "  E-slots: " + std::to_string(t.n_e_slots());
    s += "  SMT: ";
    s += t.smt_active ? "yes" : "no";
    s += "  Hybrid: ";
    s += t.hybrid ? "yes" : "no";
    s += "  Detection: ";
    s += t.detection_reliable ? "reliable" : "heuristic/fallback";
    s += "\n  P CPUs: " + util::format_cpulist(t.p_cpus);
    s += "\n  E CPUs: " + util::format_cpulist(t.e_cpus);
    return s;
}

// ── Thread pinning ────────────────────────────────────────────────────────────

bool pin_current_thread(int cpu_id) noexcept {
    if (util::getenv_bool("HS_DISABLE_PINNING")) return false;

#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu_id, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        util::warn_once("sched_setaffinity failed for CPU " + std::to_string(cpu_id) +
                        "; continuing without pinning.");
        return false;
    }
    return true;
#else
    (void)cpu_id;
    return false; // Not supported on this platform
#endif
}

bool unpin_current_thread() noexcept {
#if defined(__linux__)
    const int n = static_cast<int>(std::thread::hardware_concurrency());
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int i = 0; i < n; ++i) CPU_SET(i, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;
#else
    return false;
#endif
}

} // namespace hs
