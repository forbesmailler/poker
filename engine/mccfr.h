#pragma once

#include "infoset_store.h"
#include "game_state.h"
#include "card_abstraction.h"
#include "action_abstraction.h"
#include "hand_evaluator.h"
#include "rng.h"

namespace poker {

class MCCFR {
public:
    MCCFR(InfoSetStore& store,
           const CardAbstraction& card_abs,
           const ActionAbstraction& action_abs,
           const HandEvaluator& eval);

    // One iteration of external-sampling MCCFR
    // Returns utility for traversing_player
    double traverse(const GameState& state,
                    int traversing_player,
                    Rng& rng, int iteration);

private:
    InfoSetStore& store_;
    const CardAbstraction& card_abs_;
    const ActionAbstraction& action_abs_;
    const HandEvaluator& eval_;

    GameState sample_chance(const GameState& state, Rng& rng);
    InfoSetKey compute_key(const GameState& state, int player) const;
};

} // namespace poker
