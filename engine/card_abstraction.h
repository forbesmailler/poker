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
    void build(int num_threads = 1);

    void save(const std::string& path) const;
    void load(const std::string& path);

    int num_buckets(Street street) const;

    bool is_built() const { return built_; }

   private:
    // Preflop: 169 canonical hands (lossless)
    std::vector<Bucket> preflop_buckets_;  // size 169

    // Post-flop: rank_to_bucket_[street_idx][hand_rank] -> bucket
    // street_idx: 0=FLOP, 1=TURN, 2=RIVER
    std::array<std::vector<Bucket>, 3> rank_to_bucket_;

    bool built_ = false;

    // Map (hole0, hole1) to canonical preflop index 0-168
    static int canonical_preflop_index(Card c0, Card c1);

    void build_preflop_abstraction();
    void build_postflop_tables(const HandEvaluator& eval, int num_threads);
};

}  // namespace poker
