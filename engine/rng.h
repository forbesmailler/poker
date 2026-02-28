#pragma once

#include <cstdint>
#include <algorithm>

namespace poker {

// xoshiro256** — fast, high-quality PRNG
class Rng {
   public:
    explicit Rng(uint64_t seed) {
        // SplitMix64 to initialize state from single seed
        state_[0] = splitmix64(seed);
        state_[1] = splitmix64(state_[0]);
        state_[2] = splitmix64(state_[1]);
        state_[3] = splitmix64(state_[2]);
    }

    uint64_t next_u64() {
        const uint64_t result = rotl(state_[1] * 5, 7) * 9;
        const uint64_t t = state_[1] << 17;

        state_[2] ^= state_[0];
        state_[3] ^= state_[1];
        state_[1] ^= state_[2];
        state_[0] ^= state_[3];
        state_[2] ^= t;
        state_[3] = rotl(state_[3], 45);

        return result;
    }

    uint32_t next_u32() { return static_cast<uint32_t>(next_u64() >> 32); }

    // Uniform double in [0, 1)
    double next_double() { return static_cast<double>(next_u64() >> 11) * 0x1.0p-53; }

    // Uniform float in [0, 1)
    float next_float() { return static_cast<float>(next_u32() >> 8) * 0x1.0p-24f; }

    // Uniform int in [0, n)
    int next_int(int n) { return static_cast<int>(next_u64() % static_cast<uint64_t>(n)); }

    // Sample action index from probability distribution
    int sample_action(const float* probs, int n) {
        float r = next_float();
        float cumulative = 0.0f;
        for (int i = 0; i < n - 1; ++i) {
            cumulative += probs[i];
            if (r < cumulative)
                return i;
        }
        return n - 1;
    }

    // Fisher-Yates shuffle
    template <typename T>
    void shuffle(T* arr, int n) {
        for (int i = n - 1; i > 0; --i) {
            int j = next_int(i + 1);
            std::swap(arr[i], arr[j]);
        }
    }

   private:
    uint64_t state_[4];

    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    static uint64_t splitmix64(uint64_t x) {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }
};

}  // namespace poker
