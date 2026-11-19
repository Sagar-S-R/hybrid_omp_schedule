#pragma once
/// @file cli.hpp
/// @brief Tiny CLI argument parser: --key value and --flag style.

#include <map>
#include <set>
#include <string>
#include <vector>

namespace hs {
namespace bench {

class CLI {
public:
    /// Parse argc/argv.  Arguments must be --key value or --flag.
    void parse(int argc, char** argv);

    /// Return true if --flag is present.
    [[nodiscard]] bool flag(const std::string& name) const;

    /// Return string value of --key, or default_val if not set.
    [[nodiscard]] std::string get(const std::string& key,
                                  const std::string& default_val = "") const;

    /// Return integer value of --key, or default_val if not set or invalid.
    [[nodiscard]] int get_int(const std::string& key, int default_val = 0) const;

    /// Return double value of --key, or default_val if not set or invalid.
    [[nodiscard]] double get_double(const std::string& key, double default_val = 0.0) const;

    /// Return long value of --key, or default_val if not set or invalid.
    [[nodiscard]] long get_long(const std::string& key, long default_val = 0) const;

    /// Return all positional arguments (those not starting with --).
    [[nodiscard]] const std::vector<std::string>& positional() const { return positional_; }

    /// Return the sub-command (first positional argument), or "".
    [[nodiscard]] std::string subcommand() const;

    /// Print usage to stderr.
    static void usage(const std::string& prog, const std::string& usage_str);

private:
    std::map<std::string, std::string> kv_;
    std::set<std::string> flags_;
    std::vector<std::string> positional_;
};

} // namespace bench
} // namespace hs
