/// @file system_probe.cpp

#include "system_probe.hpp"
#include "hybrid_sched/topology.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

#if defined(__linux__)
#include <dirent.h>
#endif

namespace hs {
namespace bench {

double read_pkg_temp_c() {
#if defined(__linux__)
    // Scan /sys/class/thermal/thermal_zoneN for x86_pkg_temp
    const std::string base = "/sys/class/thermal/";
    DIR* dir = opendir(base.c_str());
    if (!dir) return -1.0;

    double best = -1.0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const std::string name(ent->d_name);
        if (name.find("thermal_zone") == std::string::npos) continue;
        const std::string type_path = base + name + "/type";
        const std::string type = util::read_first_line(type_path);
        if (type.find("x86_pkg_temp") == std::string::npos &&
            type.find("Package") == std::string::npos) continue;
        const std::string temp_path = base + name + "/temp";
        const std::string val = util::read_first_line(temp_path);
        if (!val.empty()) {
            try {
                // temp is in millidegrees Celsius
                best = std::stod(val) / 1000.0;
            } catch (...) {}
        }
    }
    closedir(dir);
    return best;
#else
    return -1.0;
#endif
}

long read_cpu_cur_khz(int cpu_id) {
#if defined(__linux__)
    const std::string path = "/sys/devices/system/cpu/cpu" +
                              std::to_string(cpu_id) + "/cpufreq/scaling_cur_freq";
    const std::string val = util::read_first_line(path);
    if (val.empty()) return 0;
    try { return std::stol(val); } catch (...) { return 0; }
#else
    (void)cpu_id;
    return 0;
#endif
}

std::string read_governor() {
#if defined(__linux__)
    return util::read_first_line(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
#else
    return "unknown";
#endif
}

bool turbo_disabled() {
#if defined(__linux__)
    const std::string val = util::read_first_line(
        "/sys/devices/system/cpu/intel_pstate/no_turbo");
    return val == "1";
#else
    return false;
#endif
}

SystemState probe_system() {
    SystemState s;
    s.pkg_temp_c = read_pkg_temp_c();
    s.governor   = read_governor();
    s.turbo_enabled = !turbo_disabled();

#if defined(__linux__)
    s.intel_pstate = !util::read_first_line(
        "/sys/devices/system/cpu/intel_pstate/status").empty();
#endif

    const Topology topo = detect_topology();
    if (!topo.p_cpus.empty())
        s.p_core_khz = read_cpu_cur_khz(topo.p_cpus[0]);
    if (!topo.e_cpus.empty())
        s.e_core_khz = read_cpu_cur_khz(topo.e_cpus[0]);

    return s;
}

} // namespace bench
} // namespace hs
