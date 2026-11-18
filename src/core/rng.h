#pragma once

#include <cstdint>

namespace soak {

inline uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// PCG32 generator with portable, platform-independent range reduction, so a
// given seed produces exactly the same sequence on every compiler and OS.
// (std::uniform_int_distribution is implementation-defined, which would make
// level N differ between platforms.)
class Rng {
public:
    explicit Rng(uint64_t seed) {
        uint64_t s = seed;
        state_ = splitmix64(s);
        inc_ = splitmix64(s) | 1u;
        next();
    }

    uint32_t next() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        const uint32_t xorshifted = uint32_t(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = uint32_t(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // Uniform integer in [0, n).
    uint32_t below(uint32_t n) {
        if (n <= 1) return 0;
        const uint32_t threshold = (0u - n) % n;
        for (;;) {
            const uint32_t r = next();
            if (r >= threshold) return r % n;
        }
    }

    // Uniform integer in [lo, hi] (inclusive).
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + int(below(uint32_t(hi - lo + 1)));
    }

private:
    uint64_t state_;
    uint64_t inc_;
};

}  // namespace soak
