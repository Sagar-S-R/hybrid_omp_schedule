/// @file uniform_fma.cpp
/// Synthetic uniform FMA kernel — identical independent work per iteration.
/// Used by the overhead suite to measure framework overhead vs loop duration.

#include "kernels/kernel.hpp"
#include "common/executor.hpp"
#include "common/rng.hpp"

#include <cmath>
#include <cstddef>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace hs {
namespace bench {

class UniformFmaKernel : public Kernel {
public:
    [[nodiscard]] std::string name() const override { return "uniform_fma"; }

    void setup(const KernelParams& p) override {
        const std::size_t defaults[] = {1 << 12, 1 << 18, 1 << 22, 1 << 24};
        n_ = p.n > 0 ? p.n : defaults[static_cast<int>(p.size_class)];
        fma_per_iter_ = p.k > 0 ? p.k : 64; // FMAs per iteration

        rng::Xoshiro256 rng(p.seed);
        data_.resize(n_);
        result_.resize(n_, 0.0f);
        for (auto& v : data_) v = static_cast<float>(rng.next_f64());
        init_data_ = data_;
        computed_ = false;
    }

    void run(Executor& ex) override {
        const std::size_t fma_count = fma_per_iter_;

        ex.parallel_for_range("uniform.fma", 0, n_,
                               [this, fma_count](std::size_t lo, std::size_t hi) {
                                   for (std::size_t i = lo; i < hi; ++i) {
                                       float v = data_[i];
                                       // fma_count independent FMA operations
                                       // volatile-load to prevent full elimination
                                       for (std::size_t f = 0; f < fma_count; ++f) {
                                           v = v * 1.000001f + 0.000001f;
                                       }
                                       result_[i] = v;
                                   }
                               });
        computed_ = true;
    }

    [[nodiscard]] bool verify() override {
        if (!computed_) return false;
        // Verify a sample: apply the same sequence serially
        const std::size_t stride = std::max<std::size_t>(1, n_ / 32);
        for (std::size_t i = 0; i < n_; i += stride) {
            float v = init_data_[i];
            for (std::size_t f = 0; f < fma_per_iter_; ++f)
                v = v * 1.000001f + 0.000001f;
            if (std::abs(result_[i] - v) > 1e-3f) return false;
        }
        return true;
    }

    void reset() override {
        std::fill(result_.begin(), result_.end(), 0.0f);
        computed_ = false;
    }

    [[nodiscard]] double work_units() const override {
        return 2.0 * static_cast<double>(n_) * static_cast<double>(fma_per_iter_);
    }

    [[nodiscard]] std::string work_unit_name() const override { return "FLOP"; }

private:
    std::size_t n_{0};
    std::size_t fma_per_iter_{64};
    std::vector<float> data_;
    std::vector<float> result_;
    std::vector<float> init_data_;
    bool computed_{false};
};

std::unique_ptr<Kernel> make_uniform_fma() {
    return std::make_unique<UniformFmaKernel>();
}

} // namespace bench
} // namespace hs
