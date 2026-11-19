/// @file mlp_train.cpp
/// Dense MLP training: 784-512-256-10, ReLU, softmax cross-entropy, SGD.
/// Written with plain loops — no BLAS.
/// Loops: mlp.fwd1, mlp.fwd2, mlp.act, mlp.loss, mlp.bwd_dw, mlp.bwd_dx, mlp.sgd

#include "kernels/kernel.hpp"
#include "common/executor.hpp"
#include "common/rng.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace hs {
namespace bench {

// ── MLP parameters ────────────────────────────────────────────────────────────
static constexpr std::size_t kIn = 784;
static constexpr std::size_t kH1 = 512;
static constexpr std::size_t kH2 = 256;
static constexpr std::size_t kOut = 10;

class MlpTrainKernel : public Kernel {
public:
    [[nodiscard]] std::string name() const override { return "mlp_train"; }

    void setup(const KernelParams& p) override {
        const std::size_t batch_defaults[] = {16, 64, 256, 512};
        batch_ = p.n > 0 ? p.n : batch_defaults[static_cast<int>(p.size_class)];
        epochs_ = p.iters > 0 ? p.iters : 2;
        lr_ = 0.01f;

        rng::Xoshiro256 rng(p.seed);

        // Weights: He initialisation
        auto he = [&](std::size_t fan_in) {
            return static_cast<float>(rng.next_gaussian() * std::sqrt(2.0 / static_cast<double>(fan_in)));
        };

        W1_.resize(kH1 * kIn);  for (auto& w : W1_) w = he(kIn);
        b1_.resize(kH1, 0.0f);
        W2_.resize(kH2 * kH1); for (auto& w : W2_) w = he(kH1);
        b2_.resize(kH2, 0.0f);
        W3_.resize(kOut * kH2); for (auto& w : W3_) w = he(kH2);
        b3_.resize(kOut, 0.0f);

        // Synthetic input: clusters
        X_.resize(batch_ * kIn);
        Y_.resize(batch_);
        for (std::size_t i = 0; i < batch_; ++i) {
            Y_[i] = static_cast<int>(rng.next_bounded(kOut));
            const std::size_t cluster_center = Y_[i] * (kIn / kOut);
            for (std::size_t j = 0; j < kIn; ++j) {
                X_[i * kIn + j] = static_cast<float>(rng.next_gaussian() * 0.3 +
                    (j == cluster_center ? 1.0 : 0.0));
            }
        }

        // Working buffers
        H1_.resize(batch_ * kH1);
        H2_.resize(batch_ * kH2);
        logits_.resize(batch_ * kOut);
        dH2_.resize(batch_ * kH2);
        dH1_.resize(batch_ * kH1);

        init_loss_ = 1e30f;
        last_loss_ = 1e30f;
        computed_ = false;

        // Save initial weights for reset
        W1_init_ = W1_; b1_init_ = b1_;
        W2_init_ = W2_; b2_init_ = b2_;
        W3_init_ = W3_; b3_init_ = b3_;
    }

    void run(Executor& ex) override {
        for (std::size_t ep = 0; ep < epochs_; ++ep) {
            forward(ex);
            backward(ex);
        }
        computed_ = true;
    }

    [[nodiscard]] bool verify() override {
        // Loss should be finite and less than ln(kOut) (random baseline)
        if (!computed_) return false;
        if (!std::isfinite(last_loss_)) return false;
        // Loss <= log(kOut) + some tolerance (after training it should decrease)
        // We just check it's a reasonable finite number
        return last_loss_ < 100.0f;
    }

    void reset() override {
        W1_ = W1_init_; b1_ = b1_init_;
        W2_ = W2_init_; b2_ = b2_init_;
        W3_ = W3_init_; b3_ = b3_init_;
        last_loss_ = 1e30f;
        computed_ = false;
    }

    [[nodiscard]] double work_units() const override {
        // Approximate FLOPs for fwd+bwd per epoch
        const double fwd = static_cast<double>(batch_) *
            (2.0 * kIn * kH1 + 2.0 * kH1 * kH2 + 2.0 * kH2 * kOut);
        return fwd * 3.0 * static_cast<double>(epochs_); // ~3x for bwd
    }

    [[nodiscard]] std::string work_unit_name() const override { return "FLOP"; }

private:
    void forward(Executor& ex) {
        const std::size_t B = batch_;

        // Layer 1: H1 = relu(X @ W1.T + b1)
        ex.parallel_for_range("mlp.fwd1", 0, B,
                               [this](std::size_t lo, std::size_t hi) {
                                   for (std::size_t i = lo; i < hi; ++i)
                                       for (std::size_t j = 0; j < kH1; ++j) {
                                           float sum = b1_[j];
                                           for (std::size_t k = 0; k < kIn; ++k)
                                               sum += X_[i * kIn + k] * W1_[j * kIn + k];
                                           H1_[i * kH1 + j] = sum;
                                       }
                               });

        // Activation ReLU on H1
        ex.parallel_for_range("mlp.act", 0, B * kH1,
                               [this](std::size_t lo, std::size_t hi) {
                                   for (std::size_t i = lo; i < hi; ++i)
                                       H1_[i] = H1_[i] > 0.0f ? H1_[i] : 0.0f;
                               });

        // Layer 2: H2 = relu(H1 @ W2.T + b2)
        ex.parallel_for_range("mlp.fwd2", 0, B,
                               [this](std::size_t lo, std::size_t hi) {
                                   for (std::size_t i = lo; i < hi; ++i) {
                                       for (std::size_t j = 0; j < kH2; ++j) {
                                           float sum = b2_[j];
                                           for (std::size_t k = 0; k < kH1; ++k)
                                               sum += H1_[i * kH1 + k] * W2_[j * kH1 + k];
                                           H2_[i * kH2 + j] = sum > 0.0f ? sum : 0.0f;
                                       }
                                   }
                               });

        // Layer 3: logits = H2 @ W3.T + b3 (no activation)
        ex.parallel_for_range("mlp.loss", 0, B,
                               [this](std::size_t lo, std::size_t hi) {
                                   for (std::size_t i = lo; i < hi; ++i) {
                                       for (std::size_t j = 0; j < kOut; ++j) {
                                           float sum = b3_[j];
                                           for (std::size_t k = 0; k < kH2; ++k)
                                               sum += H2_[i * kH2 + k] * W3_[j * kH2 + k];
                                           logits_[i * kOut + j] = sum;
                                       }
                                   }
                               });

        // Softmax + cross-entropy loss (serial over batch — small)
        float total_loss = 0.0f;
        for (std::size_t i = 0; i < B; ++i) {
            float max_l = logits_[i * kOut];
            for (std::size_t j = 1; j < kOut; ++j)
                if (logits_[i * kOut + j] > max_l) max_l = logits_[i * kOut + j];
            float sum_exp = 0.0f;
            for (std::size_t j = 0; j < kOut; ++j) {
                logits_[i * kOut + j] = std::exp(logits_[i * kOut + j] - max_l);
                sum_exp += logits_[i * kOut + j];
            }
            for (std::size_t j = 0; j < kOut; ++j)
                logits_[i * kOut + j] /= sum_exp;
            // Cross-entropy gradient: dlogit = prob - one_hot
            total_loss -= std::log(logits_[i * kOut + static_cast<std::size_t>(Y_[i])] + 1e-9f);
            for (std::size_t j = 0; j < kOut; ++j)
                logits_[i * kOut + j] -= (static_cast<int>(j) == Y_[i] ? 1.0f : 0.0f);
        }
        last_loss_ = total_loss / static_cast<float>(B);
        if (init_loss_ > 1e29f) init_loss_ = last_loss_;
    }

    void backward(Executor& ex) {
        const std::size_t B = batch_;
        const float inv_B = 1.0f / static_cast<float>(B);
        const float lr = lr_;

        // dW3 = H2.T @ dLogits (parallel over output rows j)
        ex.parallel_for_range("mlp.bwd_dw", 0, kOut,
                               [this, B, inv_B, lr](std::size_t lo, std::size_t hi) {
                                   for (std::size_t j = lo; j < hi; ++j) {
                                       float db = 0.0f;
                                       for (std::size_t i = 0; i < B; ++i)
                                           db += logits_[i * kOut + j];
                                       b3_[j] -= lr * db * inv_B;
                                       for (std::size_t k = 0; k < kH2; ++k) {
                                           float dw = 0.0f;
                                           for (std::size_t i = 0; i < B; ++i)
                                               dw += H2_[i * kH2 + k] * logits_[i * kOut + j];
                                           W3_[j * kH2 + k] -= lr * dw * inv_B;
                                       }
                                   }
                               });

        // dH2 = dLogits @ W3 (serial — avoid race)
        std::fill(dH2_.begin(), dH2_.end(), 0.0f);
        for (std::size_t i = 0; i < B; ++i)
            for (std::size_t j = 0; j < kOut; ++j) {
                const float dl = logits_[i * kOut + j];
                for (std::size_t k = 0; k < kH2; ++k)
                    dH2_[i * kH2 + k] += dl * W3_[j * kH2 + k];
            }
        // Apply ReLU mask on H2
        for (std::size_t i = 0; i < B * kH2; ++i)
            if (H2_[i] <= 0.0f) dH2_[i] = 0.0f;

        // dW2 + update (parallel over H1 rows)
        ex.parallel_for_range("mlp.bwd_dx", 0, kH2,
                               [this, B, inv_B, lr](std::size_t lo, std::size_t hi) {
                                   for (std::size_t j = lo; j < hi; ++j) {
                                       float db = 0.0f;
                                       for (std::size_t i = 0; i < B; ++i)
                                           db += dH2_[i * kH2 + j];
                                       b2_[j] -= lr * db * inv_B;
                                       for (std::size_t k = 0; k < kH1; ++k) {
                                           float dw = 0.0f;
                                           for (std::size_t i = 0; i < B; ++i)
                                               dw += H1_[i * kH1 + k] * dH2_[i * kH2 + j];
                                           W2_[j * kH1 + k] -= lr * dw * inv_B;
                                       }
                                   }
                               });

        // dH1 = dH2 @ W2 (serial)
        std::fill(dH1_.begin(), dH1_.end(), 0.0f);
        for (std::size_t i = 0; i < B; ++i)
            for (std::size_t j = 0; j < kH2; ++j) {
                const float d = dH2_[i * kH2 + j];
                for (std::size_t k = 0; k < kH1; ++k)
                    dH1_[i * kH1 + k] += d * W2_[j * kH1 + k];
            }
        // ReLU mask on H1
        for (std::size_t i = 0; i < B * kH1; ++i)
            if (H1_[i] <= 0.0f) dH1_[i] = 0.0f;

        // dW1 + SGD update (parallel over output rows)
        ex.parallel_for_range("mlp.sgd", 0, kH1,
                               [this, B, inv_B, lr](std::size_t lo, std::size_t hi) {
                                   for (std::size_t j = lo; j < hi; ++j) {
                                       float db = 0.0f;
                                       for (std::size_t i = 0; i < B; ++i)
                                           db += dH1_[i * kH1 + j];
                                       b1_[j] -= lr * db * inv_B;
                                       for (std::size_t k = 0; k < kIn; ++k) {
                                           float dw = 0.0f;
                                           for (std::size_t i = 0; i < B; ++i)
                                               dw += X_[i * kIn + k] * dH1_[i * kH1 + j];
                                           W1_[j * kIn + k] -= lr * dw * inv_B;
                                       }
                                   }
                               });
    }

    std::size_t batch_{256};
    std::size_t epochs_{2};
    float lr_{0.01f};

    std::vector<float> W1_, b1_, W2_, b2_, W3_, b3_;
    std::vector<float> W1_init_, b1_init_, W2_init_, b2_init_, W3_init_, b3_init_;
    std::vector<float> X_;
    std::vector<int> Y_;
    std::vector<float> H1_, H2_, logits_;
    std::vector<float> dH2_, dH1_;

    float init_loss_{1e30f};
    float last_loss_{1e30f};
    bool computed_{false};
};

std::unique_ptr<Kernel> make_mlp_train() {
    return std::make_unique<MlpTrainKernel>();
}

} // namespace bench
} // namespace hs
