#pragma once
/// @file util.hpp
/// @brief String/file parsing helpers, cpulist parser, env helpers.

#include <cstdlib>
#include <string>
#include <vector>

namespace hs {
namespace util {

/// Parse a Linux cpulist string like "0-3,8,10-11" into a sorted vector of CPU ids.
/// Returns an empty vector if the string is malformed (no throw).
[[nodiscard]] std::vector<int> parse_cpulist(const std::string& s);

/// Format a sorted vector of CPU ids back to a cpulist string.
[[nodiscard]] std::string format_cpulist(const std::vector<int>& cpus);

/// Read the entire contents of a file into a string.
/// Returns empty string on failure (no throw).
[[nodiscard]] std::string read_file(const std::string& path);

/// Read the first line of a file (trimmed of whitespace).
/// Returns empty string on failure.
[[nodiscard]] std::string read_first_line(const std::string& path);

/// Trim leading/trailing whitespace from a string.
[[nodiscard]] std::string trim(const std::string& s);

/// Split a string by a delimiter character.
[[nodiscard]] std::vector<std::string> split(const std::string& s, char delim);

/// Get an environment variable value; returns def if not set.
[[nodiscard]] std::string getenv_str(const char* name, const std::string& def = "");

/// Get an environment variable as a boolean (1/true/yes -> true, else false).
[[nodiscard]] bool getenv_bool(const char* name, bool def = false);

/// Get an environment variable as a long integer; returns def on failure.
[[nodiscard]] long getenv_long(const char* name, long def = 0);

/// Get an environment variable as a double; returns def on failure.
[[nodiscard]] double getenv_double(const char* name, double def = 0.0);

/// Returns true if the HS_VERBOSE environment variable is set.
[[nodiscard]] bool verbose() noexcept;

/// Print a message to stderr, prefixed with "[hybrid_sched] ".
/// Only prints if HS_VERBOSE is set or force==true.
void log(const std::string& msg, bool force = false);

/// Print a warning to stderr (always printed, once per unique message).
void warn_once(const std::string& msg);

/// Expand leading '~' in a path using $HOME.
[[nodiscard]] std::string expand_home(const std::string& path);

} // namespace util
} // namespace hs
