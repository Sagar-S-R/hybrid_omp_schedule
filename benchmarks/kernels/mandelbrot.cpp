/// @file mandelbrot.cpp
/// Mandelbrot set: parallel over rows, irregular cost per iteration.

#include "kernels/kernel.hpp"
#include "common/executor.hpp"

#include <cmath>
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace hs {
namespace bench {

class MandelbrotKernel : public Kernel {
public:
    [[nodiscard]] std::string name() const override { return "mandelbrot"; }

    void setup(const KernelParams& p) override {
        const std::size_t defaults[] = {64, 512, 1024, 2048};
        width_ = p.n > 0 ? p.n : defaults[static_cast<int>(p.size_class)];
        height_ = p.m > 0 ? p.m : width_;
        max_iter_ = p.iters > 0 ? static_cast<int>(p.iters) : 256;

        pixels_.assign(width_ * height_, 0);
        ref_pixels_.clear();
        computed_ = false;
    }

    void run(Executor& ex) override {
        const double cx_min = -2.5, cx_max = 1.0;
        const double cy_min = -1.25, cy_max = 1.25;
        const double dx = (cx_max - cx_min) / static_cast<double>(width_);
        const double dy = (cy_max - cy_min) / static_cast<double>(height_);
        const int max_it = max_iter_;
        const std::size_t W = width_;

        ex.parallel_for_range("mandel.rows", 0, height_,
                               [this, cx_min, cy_min, dx, dy, max_it, W](
                                   std::size_t row_lo, std::size_t row_hi) {
                                   for (std::size_t row = row_lo; row < row_hi; ++row) {
                                       const double cy = cy_min +
                                                         (static_cast<double>(row) + 0.5) * dy;
                                       for (std::size_t col = 0; col < W; ++col) {
                                           const double cx =
                                               cx_min + (static_cast<double>(col) + 0.5) * dx;
                                           double zx = 0.0, zy = 0.0;
                                           int iter = 0;
                                           while (zx * zx + zy * zy < 4.0 && iter < max_it) {
                                               const double tmp = zx * zx - zy * zy + cx;
                                               zy = 2.0 * zx * zy + cy;
                                               zx = tmp;
                                               ++iter;
                                           }
                                           pixels_[row * W + col] = iter;
                                       }
                                   }
                               });
        computed_ = true;
    }

    [[nodiscard]] bool verify() override {
        if (!computed_) return false;
        if (ref_pixels_.empty()) compute_reference();
        for (std::size_t i = 0; i < pixels_.size(); ++i)
            if (pixels_[i] != ref_pixels_[i]) return false;
        return true;
    }

    void reset() override {
        std::fill(pixels_.begin(), pixels_.end(), 0);
        computed_ = false;
    }

    [[nodiscard]] double work_units() const override {
        // Rough: each pixel does ~max_iter/2 complex multiplications on average
        return static_cast<double>(width_) * static_cast<double>(height_) *
               static_cast<double>(max_iter_) * 6.0; // ~6 FLOPs per iter
    }

    [[nodiscard]] std::string work_unit_name() const override { return "FLOP"; }

private:
    void compute_reference() {
        const double cx_min = -2.5, cy_min = -1.25;
        const double dx = 3.5 / static_cast<double>(width_);
        const double dy = 2.5 / static_cast<double>(height_);
        ref_pixels_.resize(width_ * height_);
        for (std::size_t row = 0; row < height_; ++row) {
            for (std::size_t col = 0; col < width_; ++col) {
                const double cx = cx_min + (static_cast<double>(col) + 0.5) * dx;
                const double cy = cy_min + (static_cast<double>(row) + 0.5) * dy;
                double zx = 0.0, zy = 0.0;
                int iter = 0;
                while (zx * zx + zy * zy < 4.0 && iter < max_iter_) {
                    const double tmp = zx * zx - zy * zy + cx;
                    zy = 2.0 * zx * zy + cy;
                    zx = tmp;
                    ++iter;
                }
                ref_pixels_[row * width_ + col] = iter;
            }
        }
    }

    std::size_t width_{0}, height_{0};
    int max_iter_{256};
    std::vector<int> pixels_;
    std::vector<int> ref_pixels_;
    bool computed_{false};
};

std::unique_ptr<Kernel> make_mandelbrot() {
    return std::make_unique<MandelbrotKernel>();
}

} // namespace bench
} // namespace hs
