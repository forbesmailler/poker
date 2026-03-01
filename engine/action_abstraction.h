#pragma once

#include "action.h"
#include "game_state.h"
#include <vector>

namespace poker {

class ActionAbstraction {
   public:
    // Default: loads bet sizes from config
    ActionAbstraction();

    // Custom: explicit bet sizes per street (for subgame solving with finer sizing)
    ActionAbstraction(const std::vector<BetSize>& preflop, const std::vector<BetSize>& flop,
                      const std::vector<BetSize>& turn, const std::vector<BetSize>& river);

    // Get legal abstract actions for current state
    std::vector<Action> get_actions(const GameState& state) const;

    // Map concrete bet to nearest abstract bet
    Action map_to_abstract(const Action& concrete, const GameState& state) const;

    int num_actions(const GameState& state) const;

   private:
    std::vector<BetSize> preflop_bet_sizes_;
    std::vector<BetSize> flop_bet_sizes_;
    std::vector<BetSize> turn_bet_sizes_;
    std::vector<BetSize> river_bet_sizes_;

    const std::vector<BetSize>& get_bet_sizes(Street street) const;
};

}  // namespace poker
