#pragma once

#include "action_abstraction.h"
#include "game_state.h"
#include "hand_evaluator.h"
#include "range_manager.h"
#include "rng.h"
#include <unordered_map>
#include <cstdint>

namespace poker {

// Subgame key: player(3b) + combo_index(11b) + action_hash(50b)
using SubgameKey = uint64_t;

inline SubgameKey make_subgame_key(int player, int combo_idx, uint64_t action_hash) {
    return (static_cast<uint64_t>(player) << 61) |
           (static_cast<uint64_t>(combo_idx & 0x7FF) << 50) | (action_hash & 0x3FFFFFFFFFFFFULL);
}

struct SubgameNodeData {
    static constexpr int MAX_ACTIONS = 8;
    float regret[MAX_ACTIONS] = {};
    float strategy_sum[MAX_ACTIONS] = {};
    uint8_t num_actions = 0;

    explicit SubgameNodeData(int n = 0) : num_actions(static_cast<uint8_t>(n)) {}

    // Current strategy via regret matching (same as InfoSetData)
    void current_strategy(float* out) const {
        float positive_sum = 0.0f;
        for (int a = 0; a < num_actions; ++a) {
            float r = std::max(regret[a], 0.0f);
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

    // Average strategy (converged output)
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

class SubgameCFR {
   public:
    SubgameCFR(const ActionAbstraction& action_abs, const HandEvaluator& eval);

    // Solve subgame. Returns hero EV.
    // If depth_limited=true, solves only the current betting round and estimates
    // turn leaf values via Monte Carlo equity (Pluribus-style depth-limited solving).
    double solve(const GameState& root, Card hero_c0, Card hero_c1, const Range& opp_range,
                 int hero_player, int opp_player, int num_iterations = 1000,
                 bool depth_limited = false, int num_equity_samples = 200);

    // Extract hero's converged strategy at root
    void get_strategy(const GameState& root, Card hero_c0, Card hero_c1, float* out,
                      int num_actions) const;

   private:
    const ActionAbstraction& action_abs_;
    const HandEvaluator& eval_;
    std::unordered_map<SubgameKey, SubgameNodeData> nodes_;
    int hero_player_ = 0;  // Set by solve()
    bool depth_limited_ = false;
    int num_equity_samples_ = 200;
    int cfr_iteration_ = 0;

    // Vanilla CFR traversal
    // Returns EV for hero_player
    double traverse(const GameState& state, Card hero_c0, Card hero_c1, const Range& opp_reach,
                    double hero_reach, int hero_player, int opp_player);

    // Compute expected value at a terminal node (fold or showdown)
    double terminal_value(const GameState& state, Card hero_c0, Card hero_c1,
                          const Range& opp_reach, int hero_player, int opp_player) const;

    // Estimate EV at a turn chance node via Monte Carlo equity sampling
    // Used in depth-limited solving to avoid expanding the turn+river tree
    double estimate_equity_ev(const GameState& state, Card hero_c0, Card hero_c1,
                              const Range& opp_reach, int hero_player, int opp_player);
};

}  // namespace poker
