/// @file conv_layer.cpp
/// Direct convolution: NCHW, 3x3, stride=1, pad=1, parallel over (batch x out_channel).

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

class ConvLayerKernel : public Kernel {
public:
    [[nodiscard]] std::string name() const override { return "conv_layer"; }

    void setup(const KernelParams& p) override {
        // Size: (N, C_in, H, W) -> (N, C_out, H, W) with 3x3 pad=1
        const std::size_t n_defaults[]    = {2,  4,  8,  16};
        const std::size_t cin_defaults[]  = {3, 16, 32,  64};
        const std::size_t cout_defaults[] = {8, 32, 64, 128};
        const std::size_t hw_defaults[]   = {8, 28, 56, 112};

        N_     = p.n > 0 ? p.n : n_defaults[static_cast<int>(p.size_class)];
        Cin_   = p.m > 0 ? p.m : cin_defaults[static_cast<int>(p.size_class)];
        Cout_  = p.k > 0 ? p.k : cout_defaults[static_cast<int>(p.size_class)];
        H_     = p.iters > 0 ? p.iters : hw_defaults[static_cast<int>(p.size_class)];
        W_     = H_;

        rng::Xoshiro256 rng(p.seed);
        input_.resize(N_ * Cin_ * H_ * W_);
        weight_.resize(Cout_ * Cin_ * 9); // 3x3 kernel
        output_.resize(N_ * Cout_ * H_ * W_, 0.0f);

        for (auto& v : input_)  v = static_cast<float>(rng.next_f64() * 0.1);
        for (auto& v : weight_) v = static_cast<float>(rng.next_f64() * 0.1);

        input_ref_  = input_;
        weight_ref_ = weight_;
        computed_ = false;
    }

    void run(Executor& ex) override {
        const std::size_t total_out = N_ * Cout_;
        ex.parallel_for_range("conv.fwd", 0, total_out,
                               [this](std::size_t lo, std::size_t hi) {
                                   direct_conv(lo, hi);
                               });

        // Optional ReLU pass
        ex.parallel_for_range("conv.relu", 0, output_.size(),
                               [this](std::size_t lo, std::size_t hi) {
                                   for (std::size_t i = lo; i < hi; ++i)
                                       output_[i] = output_[i] > 0.0f ? output_[i] : 0.0f;
                               });
        computed_ = true;
    }

    [[nodiscard]] bool verify() override {
        if (!computed_) return false;
        // Serial reference on a small sub-region
        const std::size_t n_check = std::min(N_, std::size_t{1});
        const std::size_t c_check = std::min(Cout_, std::size_t{2});
        const std::size_t h_check = std::min(H_, std::size_t{4});

        for (std::size_t n = 0; n < n_check; ++n) {
            for (std::size_t co = 0; co < c_check; ++co) {
                for (std::size_t oh = 0; oh < h_check; ++oh) {
                    for (std::size_t ow = 0; ow < std::min(W_, std::size_t{4}); ++ow) {
                        float ref = 0.0f;
                        for (std::size_t ci = 0; ci < Cin_; ++ci) {
                            for (int kh = -1; kh <= 1; ++kh) {
                                for (int kw = -1; kw <= 1; ++kw) {
                                    const int ih = static_cast<int>(oh) + kh;
                                    const int iw = static_cast<int>(ow) + kw;
                                    if (ih < 0 || iw < 0 ||
                                        ih >= static_cast<int>(H_) ||
                                        iw >= static_cast<int>(W_))
                                        continue;
                                    const std::size_t ki = (kh + 1) * 3 + (kw + 1);
                                    ref += input_ref_[n * Cin_ * H_ * W_ +
                                                      ci * H_ * W_ +
                                                      static_cast<std::size_t>(ih) * W_ +
                                                      static_cast<std::size_t>(iw)] *
                                           weight_ref_[co * Cin_ * 9 + ci * 9 + ki];
                                }
                            }
                        }
                        ref = ref > 0.0f ? ref : 0.0f; // ReLU
                        const float got =
                            output_[n * Cout_ * H_ * W_ + co * H_ * W_ + oh * W_ + ow];
                        if (std::abs(got - ref) > 1e-3f * std::abs(ref) + 1e-5f)
                            return false;
                    }
                }
            }
        }
        return true;
    }

    void reset() override {
        std::fill(output_.begin(), output_.end(), 0.0f);
        computed_ = false;
    }

    [[nodiscard]] double work_units() const override {
        return 2.0 * static_cast<double>(N_) * static_cast<double>(Cout_) *
               static_cast<double>(Cin_) * static_cast<double>(H_) *
               static_cast<double>(W_) * 9.0;
    }

    [[nodiscard]] std::string work_unit_name() const override { return "FLOP"; }

private:
    void direct_conv(std::size_t lo, std::size_t hi) {
        // lo..hi are indices into (n, co) flattened
        for (std::size_t idx = lo; idx < hi; ++idx) {
            const std::size_t n  = idx / Cout_;
            const std::size_t co = idx % Cout_;
            for (std::size_t oh = 0; oh < H_; ++oh) {
                for (std::size_t ow = 0; ow < W_; ++ow) {
                    float acc = 0.0f;
                    for (std::size_t ci = 0; ci < Cin_; ++ci) {
                        for (int kh = -1; kh <= 1; ++kh) {
                            for (int kw = -1; kw <= 1; ++kw) {
                                const int ih = static_cast<int>(oh) + kh;
                                const int iw = static_cast<int>(ow) + kw;
                                if (ih < 0 || iw < 0 ||
                                    ih >= static_cast<int>(H_) ||
                                    iw >= static_cast<int>(W_))
                                    continue;
                                const std::size_t ki = static_cast<std::size_t>(
                                    (kh + 1) * 3 + (kw + 1));
                                acc += input_[n * Cin_ * H_ * W_ +
                                              ci * H_ * W_ +
                                              static_cast<std::size_t>(ih) * W_ +
                                              static_cast<std::size_t>(iw)] *
                                       weight_[co * Cin_ * 9 + ci * 9 + ki];
                            }
                        }
                    }
                    output_[n * Cout_ * H_ * W_ + co * H_ * W_ + oh * W_ + ow] = acc;
                }
            }
        }
    }

    std::size_t N_{0}, Cin_{0}, Cout_{0}, H_{0}, W_{0};
    std::vector<float> input_, weight_, output_;
    std::vector<float> input_ref_, weight_ref_;
    bool computed_{false};
};

std::unique_ptr<Kernel> make_conv_layer() {
    return std::make_unique<ConvLayerKernel>();
}

} // namespace bench
} // namespace hs
