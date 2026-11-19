/// @file power_model.cpp

#include "hybrid_sched/power_model.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

namespace hs {

// ── Pure parsing / serialisation ──────────────────────────────────────────────

PowerProfile parse_power_profile(const std::string& content) {
    PowerProfile p;
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        line = util::trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = util::trim(line.substr(0, eq));
        const std::string val = util::trim(line.substr(eq + 1));
        if (key == "base_w") {
            try { p.base_w = std::stod(val); } catch (...) {}
        } else if (key == "p_phys_w") {
            try { p.p_phys_w = std::stod(val); } catch (...) {}
        } else if (key == "p_smt_w") {
            try { p.p_smt_w = std::stod(val); } catch (...) {}
        } else if (key == "p_e_w") {
            try { p.p_e_w = std::stod(val); } catch (...) {}
        } else if (key == "mem_scale") {
            try { p.mem_scale = std::stod(val); } catch (...) {}
        } else if (key == "cpu_model") {
            p.cpu_model = val;
        } else if (key == "n_p_phys") {
            try { p.n_p_phys = std::stoi(val); } catch (...) {}
        } else if (key == "n_p_logical") {
            try { p.n_p_logical = std::stoi(val); } catch (...) {}
        } else if (key == "n_e") {
            try { p.n_e = std::stoi(val); } catch (...) {}
        } else if (key == "timestamp") {
            p.timestamp = val;
        }
    }
    p.profile_loaded = true;
    return p;
}

std::string serialize_power_profile(const PowerProfile& p) {
    std::ostringstream ss;
    ss << "# hybrid_sched power profile\n";
    ss << "base_w=" << p.base_w << "\n";
    ss << "p_phys_w=" << p.p_phys_w << "\n";
    ss << "p_smt_w=" << p.p_smt_w << "\n";
    ss << "p_e_w=" << p.p_e_w << "\n";
    ss << "mem_scale=" << p.mem_scale << "\n";
    ss << "cpu_model=" << p.cpu_model << "\n";
    ss << "n_p_phys=" << p.n_p_phys << "\n";
    ss << "n_p_logical=" << p.n_p_logical << "\n";
    ss << "n_e=" << p.n_e << "\n";
    ss << "timestamp=" << p.timestamp << "\n";
    return ss.str();
}

// ── PowerModel ────────────────────────────────────────────────────────────────

PowerModel::PowerModel() {
    // Try to load profile; silently use defaults if not found
    load();
}

bool PowerModel::load(const std::string& path) {
    // Search order: explicit path, HS_POWER_PROFILE env, ./power_profile.txt,
    //               ~/.config/hybrid_sched/power_profile.txt
    std::vector<std::string> search_paths;
    if (!path.empty()) search_paths.push_back(path);

    const std::string env_path = util::getenv_str("HS_POWER_PROFILE");
    if (!env_path.empty()) search_paths.push_back(env_path);

    search_paths.push_back("./power_profile.txt");
    search_paths.push_back(
        util::expand_home("~/.config/hybrid_sched/power_profile.txt"));

    for (const auto& p : search_paths) {
        const std::string content = util::read_file(p);
        if (!content.empty()) {
            profile_ = parse_power_profile(content);
            util::log("Power profile loaded from: " + p);
            return true;
        }
    }

    // Use defaults
    util::warn_once(
        "No power profile found. Using rough defaults "
        "(base=3W, p_phys=4W, p_smt=0.8W, p_e=1W). "
        "Results with these defaults are not publishable. "
        "Run hs_power_profile to generate a profile.");
    profile_ = PowerProfile{}; // defaults
    profile_.profile_loaded = false;
    return false;
}

double PowerModel::predict_w(int a_p_logical, int b_e,
                               int n_p_phys_topology) const noexcept {
    // How many of the a_p_logical are primary (physical) and how many are SMT siblings?
    const int a_phys = std::min(a_p_logical, n_p_phys_topology);
    const int a_smt = std::max(0, a_p_logical - n_p_phys_topology);

    return profile_.base_w + static_cast<double>(a_phys) * profile_.p_phys_w +
           static_cast<double>(a_smt) * profile_.p_smt_w +
           static_cast<double>(b_e) * profile_.p_e_w;
}

bool PowerModel::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << serialize_power_profile(profile_);
    return f.good();
}

} // namespace hs
