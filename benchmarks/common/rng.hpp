#pragma once
/// @file rng.hpp
/// @brief Deterministic xoshiro256** / splitmix64 RNG — no stdlib rand().

#include <cmath>
#include <cstdint>
#include <cstring>

namespace hs {
namespace rng {

/// splitmix64 — used to seed xoshiro256**
inline std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/// xoshiro256** state
struct Xoshiro256 {
    std::uint64_t s[4];

    explicit Xoshiro256(std::uint64_t seed = 42) noexcept {
        std::uint64_t st = seed;
        s[0] = splitmix64(st);
        s[1] = splitmix64(st);
        s[2] = splitmix64(st);
        s[3] = splitmix64(st);
    }

    [[nodiscard]] std::uint64_t next() noexcept {
        const std::uint64_t result = rotl(s[1] * 5, 7) * 9;
        const std::uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }

    /// Uniform [0, n)
    [[nodiscard]] std::uint64_t next_bounded(std::uint64_t n) noexcept {
        if (n == 0) return 0;
        return next() % n;
    }

    /// Uniform float in [0, 1)
    [[nodiscard]] float next_f32() noexcept {
        const std::uint32_t bits =
            static_cast<std::uint32_t>(next() >> 41) | 0x3f800000u;
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f - 1.0f;
    }

    /// Uniform double in [0, 1)
    [[nodiscard]] double next_f64() noexcept {
        return static_cast<double>(next() >> 11) * (1.0 / (1ULL << 53));
    }

    /// Gaussian N(0,1) via Box-Muller
    [[nodiscard]] double next_gaussian() noexcept {
        const double u1 = next_f64() + 1e-300; // avoid log(0)
        const double u2 = next_f64();
        // Box-Muller
        const double r = std::sqrt(-2.0 * std::log(u1));
        const double theta = 6.283185307179586 * u2;
        return r * std::cos(theta);
    }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) noexcept {
        return (x << k) | (x >> (64 - k));
    }
};

} // namespace rng
} // namespace hs
