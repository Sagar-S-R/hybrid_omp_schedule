/// @file util.cpp

#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <unordered_set>

namespace hs {
namespace util {

// ── cpulist parser ────────────────────────────────────────────────────────────

std::vector<int> parse_cpulist(const std::string& s) {
    std::vector<int> result;
    if (s.empty()) return result;

    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = trim(token);
        if (token.empty()) continue;
        const std::size_t dash = token.find('-');
        if (dash == std::string::npos) {
            // Single CPU
            try {
                result.push_back(std::stoi(token));
            } catch (...) {
                return {};
            }
        } else {
            // Range lo-hi
            try {
                const int lo = std::stoi(token.substr(0, dash));
                const int hi = std::stoi(token.substr(dash + 1));
                if (lo > hi) return {};
                for (int i = lo; i <= hi; ++i) result.push_back(i);
            } catch (...) {
                return {};
            }
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::string format_cpulist(const std::vector<int>& cpus) {
    if (cpus.empty()) return "";
    std::string out;
    std::size_t i = 0;
    while (i < cpus.size()) {
        std::size_t j = i;
        while (j + 1 < cpus.size() && cpus[j + 1] == cpus[j] + 1) ++j;
        if (!out.empty()) out += ',';
        out += std::to_string(cpus[i]);
        if (j > i) out += '-' + std::to_string(cpus[j]);
        i = j + 1;
    }
    return out;
}

// ── File helpers ──────────────────────────────────────────────────────────────

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string read_first_line(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::string line;
    std::getline(f, line);
    return trim(line);
}

// ── String helpers ────────────────────────────────────────────────────────────

std::string trim(const std::string& s) {
    const std::size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const std::size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, delim)) result.push_back(token);
    return result;
}

// ── Env helpers ───────────────────────────────────────────────────────────────

std::string getenv_str(const char* name, const std::string& def) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : def;
}

bool getenv_bool(const char* name, bool def) {
    const char* val = std::getenv(name);
    if (!val) return def;
    std::string s = trim(std::string(val));
    if (s == "1" || s == "true" || s == "yes" || s == "TRUE" || s == "YES") return true;
    if (s == "0" || s == "false" || s == "no" || s == "FALSE" || s == "NO") return false;
    return def;
}

long getenv_long(const char* name, long def) {
    const char* val = std::getenv(name);
    if (!val) return def;
    try {
        return std::stol(val);
    } catch (...) {
        return def;
    }
}

double getenv_double(const char* name, double def) {
    const char* val = std::getenv(name);
    if (!val) return def;
    try {
        return std::stod(val);
    } catch (...) {
        return def;
    }
}

// ── Diagnostics ───────────────────────────────────────────────────────────────

bool verbose() noexcept {
    static const bool v = []() {
        const char* val = std::getenv("HS_VERBOSE");
        return val != nullptr && val[0] != '0';
    }();
    return v;
}

void log(const std::string& msg, bool force) {
    if (force || verbose()) {
        std::cerr << "[hybrid_sched] " << msg << '\n';
    }
}

void warn_once(const std::string& msg) {
    static std::mutex mtx;
    static std::unordered_set<std::string> seen;
    std::lock_guard<std::mutex> lock(mtx);
    if (seen.insert(msg).second) {
        std::cerr << "[hybrid_sched WARNING] " << msg << '\n';
    }
}

// ── Path helpers ──────────────────────────────────────────────────────────────

std::string expand_home(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

} // namespace util
} // namespace hs
