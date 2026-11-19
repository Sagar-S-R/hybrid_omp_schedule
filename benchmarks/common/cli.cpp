/// @file cli.cpp

#include "cli.hpp"

#include <iostream>
#include <stdexcept>

namespace hs {
namespace bench {

void CLI::parse(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-') {
            const std::string key = arg.substr(2);
            // Check if next arg is a value (doesn't start with --)
            if (i + 1 < argc) {
                const std::string next(argv[i + 1]);
                if (next.size() < 2 || next[0] != '-' || next[1] != '-') {
                    kv_[key] = next;
                    ++i;
                    continue;
                }
            }
            flags_.insert(key);
        } else {
            positional_.push_back(arg);
        }
    }
}

bool CLI::flag(const std::string& name) const {
    return flags_.count(name) > 0;
}

std::string CLI::get(const std::string& key, const std::string& default_val) const {
    const auto it = kv_.find(key);
    return (it != kv_.end()) ? it->second : default_val;
}

int CLI::get_int(const std::string& key, int default_val) const {
    const auto it = kv_.find(key);
    if (it == kv_.end()) return default_val;
    try { return std::stoi(it->second); } catch (...) { return default_val; }
}

double CLI::get_double(const std::string& key, double default_val) const {
    const auto it = kv_.find(key);
    if (it == kv_.end()) return default_val;
    try { return std::stod(it->second); } catch (...) { return default_val; }
}

long CLI::get_long(const std::string& key, long default_val) const {
    const auto it = kv_.find(key);
    if (it == kv_.end()) return default_val;
    try { return std::stol(it->second); } catch (...) { return default_val; }
}

std::string CLI::subcommand() const {
    return positional_.empty() ? "" : positional_[0];
}

void CLI::usage(const std::string& prog, const std::string& usage_str) {
    std::cerr << "Usage: " << prog << " " << usage_str << "\n";
}

} // namespace bench
} // namespace hs
