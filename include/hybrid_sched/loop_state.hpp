#pragma once
/// @file loop_state.hpp
/// @brief Per-loop-name persistent state: cached speeds, kappa, call counter, drift detection.

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace hs {

/// Recalibration policy.
enum class Recalibrate {
    EVERY_CALL, ///< Always calibrate (per-call, our main claim)
    EVERY_N,    ///< Calibrate every N calls
    ON_DRIFT,   ///< Calibrate when imbalance/prediction error exceeds threshold
    ONCE        ///< Calibrate only on the first call
};

/// Persistent state for one named loop.
struct LoopState {
    std::string name;
    std::vector<double> cached_speeds;    ///< Last measured per-slot speeds (iters/s)
    double kappa{1.0};                    ///< Online power scale factor
    std::size_t call_count{0};            ///< Total calls to this loop
    double last_imbalance{0.0};           ///< Imbalance fraction from last call
    double last_pred_err{0.0};            ///< |T_pred - T_meas| / T_meas from last call
    int last_a{0};                        ///< P-slots used in last call
    int last_b{0};                        ///< E-slots used in last call
    bool ever_calibrated{false};          ///< True after first calibration

    /// Decide whether to recalibrate on this call.
    [[nodiscard]] bool needs_calibration(Recalibrate mode, int every_n,
                                          double drift_threshold) const noexcept {
        switch (mode) {
        case Recalibrate::EVERY_CALL: return true;
        case Recalibrate::ONCE:       return !ever_calibrated;
        case Recalibrate::EVERY_N:
            return !ever_calibrated ||
                   (call_count % static_cast<std::size_t>(every_n) == 0);
        case Recalibrate::ON_DRIFT:
            return !ever_calibrated ||
                   last_imbalance > drift_threshold ||
                   last_pred_err > drift_threshold;
        }
        return true;
    }

    /// Update kappa using an exponential moving average.
    void update_kappa(double kappa_new, double alpha = 0.3) noexcept {
        kappa = (1.0 - alpha) * kappa + alpha * kappa_new;
    }
};

/// Thread-safe registry of all named loop states.
class LoopStateRegistry {
public:
    LoopStateRegistry() = default;
    ~LoopStateRegistry() = default;

    /// Get-or-create state for a loop name.  Thread-safe.
    LoopState& get(const std::string& name);

    /// Reset all states (e.g. for testing).
    void reset();

private:
    // Uses a simple sorted vector + mutex for low overhead (few loops expected)
    struct Impl;
    // Forward declaration: actual storage in loop_state.cpp via pimpl
    std::vector<LoopState> states_;
    // We use a separate mutex in the .cpp to avoid including <mutex> here
};

} // namespace hs
