#pragma once
/// @file power_model.hpp
/// @brief Linear power model for the hybrid CPU: P = base + a_phys*p_phys + a_smt*p_smt + b*p_e
///
/// Profile is loaded from a plain-text key=value file.  If no profile exists,
/// documented rough defaults are used and profile_loaded is false.

#include <string>

namespace hs {

/// Power model parameters.
struct PowerProfile {
    double base_w{3.0};       ///< Idle package power (W)
    double p_phys_w{4.0};     ///< Incremental power per active P physical core (W)
    double p_smt_w{0.8};      ///< Incremental power per active P SMT sibling (W)
    double p_e_w{1.0};        ///< Incremental power per active E core (W)
    double mem_scale{1.0};    ///< Scale factor for memory-bound loops (optional, default 1)

    // Metadata from the profile file (informational only)
    std::string cpu_model;
    int n_p_phys{0};
    int n_p_logical{0};
    int n_e{0};
    std::string timestamp;

    bool profile_loaded{false};  ///< False if using built-in defaults
};

class PowerModel {
public:
    explicit PowerModel();

    /// Load profile from path.  If path is empty, searches default locations.
    /// Returns true if a file was found and parsed successfully.
    bool load(const std::string& path = "");

    /// Predicted package power for (a P-logical, b E) active threads.
    /// n_p_phys_topology = number of distinct P physical cores in the topology
    ///   (to determine how many additional threads are SMT vs physical).
    [[nodiscard]] double predict_w(int a_p_logical, int b_e,
                                    int n_p_phys_topology) const noexcept;

    [[nodiscard]] const PowerProfile& profile() const noexcept { return profile_; }
    [[nodiscard]] bool profile_loaded() const noexcept { return profile_.profile_loaded; }

    /// Write current profile to a file in key=value format.
    bool save(const std::string& path) const;

private:
    PowerProfile profile_;
};

/// Parse a key=value profile file into a PowerProfile.
/// Pure function — used in unit tests.
[[nodiscard]] PowerProfile parse_power_profile(const std::string& content);

/// Serialize a PowerProfile to key=value text.
[[nodiscard]] std::string serialize_power_profile(const PowerProfile& p);

} // namespace hs
