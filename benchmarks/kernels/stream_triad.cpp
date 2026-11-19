/// @file stream_triad.cpp
/// STREAM triad: a[i] = b[i] + s * c[i]  — memory-bound.

#include "kernels/kernel.hpp"
#include "common/executor.hpp"
#include "common/rng.hpp"

#include <cmath>
#include <cstddef>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace hs {
namespace bench {

class StreamTriadKernel : public Kernel {
public:
    [[nodiscard]] std::string name() const override { return "stream_triad"; }

    void setup(const KernelParams& p) override {
        // Size defaults per class
        const std::size_t defaults[] = {1 << 16, 1 << 22, 1 << 24, 1 << 26};
        n_ = p.n > 0 ? p.n : defaults[static_cast<int>(p.size_class)];
        reps_ = p.iters > 0 ? p.iters : 3;
        scalar_ = 3.14159265358979;

        rng::Xoshiro256 rng(p.seed);
        a_.resize(n_);
        b_.resize(n_);
        c_.resize(n_);
        for (std::size_t i = 0; i < n_; ++i) {
            b_[i] = rng.next_f64() * 2.0 - 1.0;
            c_[i] = rng.next_f64() * 2.0 - 1.0;
            a_[i] = 0.0;
        }
        b_init_ = b_;
        c_init_ = c_;
        computed_ = false;
    }

    void run(Executor& ex) override {
        for (std::size_t rep = 0; rep < reps_; ++rep) {
            ex.parallel_for_range("stream.triad", 0, n_,
                                   [this](std::size_t lo, std::size_t hi) {
                                       for (std::size_t i = lo; i < hi; ++i)
                                           a_[i] = b_[i] + scalar_ * c_[i];
                                   });
        }
        computed_ = true;
    }

    [[nodiscard]] bool verify() override {
        if (!computed_) return false;
        // Check 32 evenly-spaced samples
        const std::size_t stride = std::max<std::size_t>(1, n_ / 32);
        for (std::size_t i = 0; i < n_; i += stride) {
            const double expected = b_init_[i] + scalar_ * c_init_[i];
            if (std::abs(a_[i] - expected) > 1e-10 * std::abs(expected) + 1e-12)
                return false;
        }
        return true;
    }

    void reset() override {
        std::fill(a_.begin(), a_.end(), 0.0);
        computed_ = false;
    }

    [[nodiscard]] double work_units() const override {
        // 2 FLOPs per iteration * n * reps
        return static_cast<double>(2 * n_ * reps_);
    }

    [[nodiscard]] std::string work_unit_name() const override { return "FLOP"; }

private:
    std::size_t n_{0};
    std::size_t reps_{3};
    double scalar_{0.0};
    std::vector<double> a_, b_, c_;
    std::vector<double> b_init_, c_init_;
    bool computed_{false};
};

// Factory function used by registry
std::unique_ptr<Kernel> make_stream_triad() {
    return std::make_unique<StreamTriadKernel>();
}

} // namespace bench
} // namespace hs
