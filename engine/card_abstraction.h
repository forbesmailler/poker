#pragma once

#include "card.h"
#include "hand_evaluator.h"
#include "game_state.h"
#include <cstdint>
#include <vector>
#include <array>
#include <string>

namespace poker {

using Bucket = uint16_t;

class CardAbstraction {
   public:
    CardAbstraction();

    Bucket get_bucket(Street street, Card hole0, Card hole1, const Card* board,
                      int num_board_cards) const;

    // Precompute all bucket assignments (expensive, run once)
    // max_combos_per_street: 0 = exhaustive (production), >0 = sample cap (fast tests)
    void build(int num_threads = 1, int64_t max_combos_per_street = 0);

    // Build only preflop abstraction (fast, for tests that don't need postflop)
    void build_preflop_only();

    void save(const std::string& path) const;
    void load(const std::string& path);

    int num_buckets(Street street) const;

    bool is_built() const { return built_; }

   private:
    // Preflop: 169 canonical hands (lossless)
    std::vector<Bucket> preflop_buckets_;  // size 169

    // Post-flop bucket tables:
    // [0]=FLOP: hand_rank -> bucket  (size MAX_HAND_RANK+1)
    // [1]=TURN: discretized avg river percentile -> bucket  (size varies)
    // [2]=RIVER: hand_rank -> bucket  (size MAX_HAND_RANK+1)
    std::array<std::vector<Bucket>, 3> rank_to_bucket_;

    // Resolution for turn avg-percentile discretization (bins per river bucket)
    static constexpr int TURN_AVG_RESOLUTION = 100;

    bool built_ = false;

    // Map (hole0, hole1) to canonical preflop index 0-168
    static int canonical_preflop_index(Card c0, Card c1);

    void build_preflop_abstraction();
    void build_postflop_tables(const HandEvaluator& eval, int num_threads,
                               int64_t max_combos_per_street);
};

}  // namespace poker
