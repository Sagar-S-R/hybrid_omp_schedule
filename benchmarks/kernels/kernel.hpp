#pragma once
/// @file kernel.hpp
/// @brief Kernel interface: setup, run, verify, work_units.

#include "common/executor.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace hs {
namespace bench {

/// Size classes for the kernels.
enum class SizeClass : int { Tiny = 0, Small = 1, Medium = 2, Large = 3 };

struct KernelParams {
    SizeClass size_class{SizeClass::Medium};
    std::uint64_t seed{42};
    // Kernel-specific overrides (0 = use size-class default)
    std::size_t n{0};         // generic size (rows, pixels, etc.)
    std::size_t m{0};         // secondary dimension
    std::size_t k{0};         // tertiary dimension
    std::size_t iters{0};     // iterations / epochs
};

class Kernel {
public:
    virtual ~Kernel() = default;

    [[nodiscard]] virtual std::string name() const = 0;

    /// Allocate and initialise data (not timed).
    virtual void setup(const KernelParams& params) = 0;

    /// The timed part: all parallel loops go through ex.
    virtual void run(Executor& ex) = 0;

    /// Compare parallel result against a serial reference.
    /// Returns true if correct (within tolerance for FP).
    [[nodiscard]] virtual bool verify() = 0;

    /// Restore initial state between repetitions (data reset without reallocation).
    virtual void reset() = 0;

    /// FLOPs, bytes, or other domain-specific work units.
    [[nodiscard]] virtual double work_units() const = 0;

    [[nodiscard]] virtual std::string work_unit_name() const = 0;
};

} // namespace bench
} // namespace hs
