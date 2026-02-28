#include "action_abstraction.h"
#include "generated_config.h"
#include <algorithm>
#include <cmath>

namespace poker {

ActionAbstraction::ActionAbstraction() {
    // Initialize bet sizes from config
    for (float f : config::PREFLOP_RAISE_SIZES) {
        preflop_bet_sizes_.push_back({f, false});
    }
    for (float f : config::FLOP_BET_SIZES) {
        flop_bet_sizes_.push_back({f, false});
    }
    for (float f : config::TURN_BET_SIZES) {
        turn_bet_sizes_.push_back({f, false});
    }
    for (float f : config::RIVER_BET_SIZES) {
        river_bet_sizes_.push_back({f, false});
    }

    // Add all-in option if configured
    if (config::INCLUDE_ALL_IN) {
        preflop_bet_sizes_.push_back({0.0f, true});
        flop_bet_sizes_.push_back({0.0f, true});
        turn_bet_sizes_.push_back({0.0f, true});
        river_bet_sizes_.push_back({0.0f, true});
    }
}

const std::vector<BetSize>& ActionAbstraction::get_bet_sizes(
    Street street
) const {
    switch (street) {
        case Street::PREFLOP: return preflop_bet_sizes_;
        case Street::FLOP: return flop_bet_sizes_;
        case Street::TURN: return turn_bet_sizes_;
        case Street::RIVER: return river_bet_sizes_;
        default: return flop_bet_sizes_;
    }
}

std::vector<Action> ActionAbstraction::get_actions(
    const GameState& state
) const {
    const auto& bet_sizes = get_bet_sizes(state.street());
    return state.legal_actions(bet_sizes);
}

Action ActionAbstraction::map_to_abstract(const Action& concrete,
                                           const GameState& state) const {
    if (concrete.type != ActionType::BET) return concrete;

    auto actions = get_actions(state);
    int best_idx = -1;
    int min_diff = std::numeric_limits<int>::max();

    for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
        if (actions[i].type == ActionType::BET) {
            int diff = std::abs(actions[i].amount - concrete.amount);
            if (diff < min_diff) {
                min_diff = diff;
                best_idx = i;
            }
        }
    }

    if (best_idx >= 0) return actions[best_idx];
    return concrete;
}

int ActionAbstraction::num_actions(const GameState& state) const {
    return static_cast<int>(get_actions(state).size());
}

} // namespace poker
