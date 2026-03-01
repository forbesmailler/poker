#pragma once

#include "card.h"
#include "infoset_store.h"
#include "card_abstraction.h"
#include "action_abstraction.h"
#include "game_state.h"
#include <vector>
#include <utility>

namespace poker {

// Dense range: probability weight for each of 1326 hole card combos
struct Range {
    static constexpr int NUM_COMBOS = 1326;  // C(52,2)
    float weights[NUM_COMBOS] = {};

    // Zero out combos that conflict with dead cards
    void remove_blockers(CardMask dead);

    // Scale weights so they sum to 1
    void normalize();

    // Set uniform weights for all non-blocked combos
    void init_uniform(CardMask dead);

    // Canonical index: c0 < c1 -> c1*(c1-1)/2 + c0
    static int combo_index(Card c0, Card c1);
    static void combo_from_index(int idx, Card& c0, Card& c1);
};

class RangeManager {
   public:
    RangeManager(const InfoSetStore& blueprint, const CardAbstraction& card_abs,
                 const ActionAbstraction& action_abs);

    // Walk action history, Bayesian-filter opponent range
    Range build_opponent_range(int opponent_player,
                               const std::vector<std::pair<GameState, Action>>& action_history,
                               Card hero_c0, Card hero_c1, const Card* board, int num_board) const;

   private:
    const InfoSetStore& blueprint_;
    const CardAbstraction& card_abs_;
    const ActionAbstraction& action_abs_;

    // Multiply each combo's weight by blueprint P(action_taken | combo)
    void apply_action_filter(Range& range, int player, const GameState& state_before,
                             const Action& action_taken) const;
};

}  // namespace poker
