/// @file matmul.cpp
/// Blocked float GEMM: C = A * B, parallel over row blocks.

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

class MatmulKernel : public Kernel {
public:
    [[nodiscard]] std::string name() const override { return "matmul"; }

    void setup(const KernelParams& p) override {
        const std::size_t defaults[] = {32, 256, 512, 1024};
        n_ = p.n > 0 ? p.n : defaults[static_cast<int>(p.size_class)];
        block_ = 64; // tile size

        rng::Xoshiro256 rng(p.seed);
        A_.resize(n_ * n_);
        B_.resize(n_ * n_);
        C_.resize(n_ * n_, 0.0f);

        for (auto& v : A_) v = static_cast<float>(rng.next_f64() * 2.0 - 1.0);
        for (auto& v : B_) v = static_cast<float>(rng.next_f64() * 2.0 - 1.0);

        A_ref_ = A_;
        B_ref_ = B_;
        computed_ = false;
    }

    void run(Executor& ex) override {
        const std::size_t N = n_;
        const std::size_t BLK = block_;

        // Parallel over row tiles
        const std::size_t n_row_tiles = (N + BLK - 1) / BLK;

        ex.parallel_for_range("matmul.rows", 0, n_row_tiles,
                               [this, N, BLK](std::size_t tile_lo, std::size_t tile_hi) {
                                   for (std::size_t tile_i = tile_lo; tile_i < tile_hi; ++tile_i) {
                                       const std::size_t i0 = tile_i * BLK;
                                       const std::size_t i1 = std::min(i0 + BLK, N);
                                       for (std::size_t tile_k = 0; tile_k < (N + BLK - 1) / BLK;
                                            ++tile_k) {
                                           const std::size_t k0 = tile_k * BLK;
                                           const std::size_t k1 = std::min(k0 + BLK, N);
                                           for (std::size_t tile_j = 0;
                                                tile_j < (N + BLK - 1) / BLK; ++tile_j) {
                                               const std::size_t j0 = tile_j * BLK;
                                               const std::size_t j1 = std::min(j0 + BLK, N);
                                               // Micro-kernel
                                               for (std::size_t i = i0; i < i1; ++i)
                                                   for (std::size_t k = k0; k < k1; ++k) {
                                                       const float aik = A_[i * N + k];
                                                       for (std::size_t j = j0; j < j1; ++j)
                                                           C_[i * N + j] +=
                                                               aik * B_[k * N + j];
                                                   }
                                           }
                                       }
                                   }
                               });
        computed_ = true;
    }

    [[nodiscard]] bool verify() override {
        if (!computed_) return false;
        // Spot-check 16 random rows against naive multiplication
        const std::size_t N = n_;
        const std::size_t stride = std::max<std::size_t>(1, N / 16);
        for (std::size_t i = 0; i < N; i += stride) {
            for (std::size_t j = 0; j < std::min(N, std::size_t{8}); ++j) {
                double ref = 0.0;
                for (std::size_t k = 0; k < N; ++k)
                    ref += static_cast<double>(A_ref_[i * N + k]) *
                           static_cast<double>(B_ref_[k * N + j]);
                const double diff = std::abs(static_cast<double>(C_[i * N + j]) - ref);
                const double tol = 1e-3 * std::abs(ref) + 1e-4;
                if (diff > tol) return false;
            }
        }
        return true;
    }

    void reset() override {
        std::fill(C_.begin(), C_.end(), 0.0f);
        computed_ = false;
    }

    [[nodiscard]] double work_units() const override {
        return 2.0 * static_cast<double>(n_) * static_cast<double>(n_) *
               static_cast<double>(n_);
    }

    [[nodiscard]] std::string work_unit_name() const override { return "FLOP"; }

private:
    std::size_t n_{0};
    std::size_t block_{64};
    std::vector<float> A_, B_, C_;
    std::vector<float> A_ref_, B_ref_;
    bool computed_{false};
};

std::unique_ptr<Kernel> make_matmul() {
    return std::make_unique<MatmulKernel>();
}

} // namespace bench
} // namespace hs
