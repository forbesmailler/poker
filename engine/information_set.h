#pragma once

#include <cstdint>
#include <algorithm>

namespace poker {

using InfoSetKey = uint64_t;

// Encode: player(3b) + street(2b) + card_bucket(16b) + action_hash(43b)
inline InfoSetKey make_infoset_key(int player, int street, uint16_t card_bucket,
                                   uint64_t action_hash) {
    return (static_cast<uint64_t>(player) << 61) | (static_cast<uint64_t>(street & 0x3) << 59) |
           (static_cast<uint64_t>(card_bucket) << 43) | (action_hash & 0x7FFFFFFFFFFULL);
}

struct InfoSetData {
    static constexpr int MAX_ACTIONS = 10;

    float cumulative_regret[MAX_ACTIONS] = {};
    float strategy_sum[MAX_ACTIONS] = {};
    uint8_t num_actions = 0;

    explicit InfoSetData(int n = 0) : num_actions(static_cast<uint8_t>(n)) {}

    // Current strategy via regret matching
    void current_strategy(float* out) const {
        float positive_sum = 0.0f;
        for (int a = 0; a < num_actions; ++a) {
            float r = std::max(cumulative_regret[a], 0.0f);
            out[a] = r;
            positive_sum += r;
        }
        if (positive_sum > 0.0f) {
            for (int a = 0; a < num_actions; ++a)
                out[a] /= positive_sum;
        } else {
            float uniform = 1.0f / num_actions;
            for (int a = 0; a < num_actions; ++a)
                out[a] = uniform;
        }
    }

    // Average strategy (the converged GTO output)
    void average_strategy(float* out) const {
        float total = 0.0f;
        for (int a = 0; a < num_actions; ++a)
            total += strategy_sum[a];
        if (total > 0.0f) {
            for (int a = 0; a < num_actions; ++a)
                out[a] = strategy_sum[a] / total;
        } else {
            float uniform = 1.0f / num_actions;
            for (int a = 0; a < num_actions; ++a)
                out[a] = uniform;
        }
    }
};

}  // namespace poker
