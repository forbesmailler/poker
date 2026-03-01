#include "range_manager.h"
#include <cmath>
#include <cassert>

namespace poker {

// ---------- Range ----------

int Range::combo_index(Card c0, Card c1) {
    // Ensure c0 < c1 for canonical ordering
    if (c0 > c1)
        std::swap(c0, c1);
    return c1 * (c1 - 1) / 2 + c0;
}

void Range::combo_from_index(int idx, Card& c0, Card& c1) {
    // Inverse of c1*(c1-1)/2 + c0
    // c1 is the smallest integer where c1*(c1-1)/2 > idx
    c1 = static_cast<Card>(static_cast<int>((1.0 + std::sqrt(1.0 + 8.0 * idx)) / 2.0));
    // Adjust if overshoot due to floating point
    while (c1 * (c1 - 1) / 2 > idx)
        c1--;
    while ((c1 + 1) * c1 / 2 <= idx)
        c1++;
    c0 = static_cast<Card>(idx - c1 * (c1 - 1) / 2);
}

void Range::remove_blockers(CardMask dead) {
    for (int i = 0; i < NUM_COMBOS; ++i) {
        Card c0, c1;
        combo_from_index(i, c0, c1);
        if ((dead & card_bit(c0)) || (dead & card_bit(c1))) {
            weights[i] = 0.0f;
        }
    }
}

void Range::normalize() {
    float total = 0.0f;
    for (int i = 0; i < NUM_COMBOS; ++i)
        total += weights[i];
    if (total > 0.0f) {
        float inv = 1.0f / total;
        for (int i = 0; i < NUM_COMBOS; ++i)
            weights[i] *= inv;
    }
}

void Range::init_uniform(CardMask dead) {
    int live = 0;
    for (int i = 0; i < NUM_COMBOS; ++i) {
        Card c0, c1;
        combo_from_index(i, c0, c1);
        if ((dead & card_bit(c0)) || (dead & card_bit(c1))) {
            weights[i] = 0.0f;
        } else {
            weights[i] = 1.0f;
            live++;
        }
    }
    if (live > 0) {
        float w = 1.0f / static_cast<float>(live);
        for (int i = 0; i < NUM_COMBOS; ++i)
            if (weights[i] > 0.0f)
                weights[i] = w;
    }
}

// ---------- RangeManager ----------

RangeManager::RangeManager(const InfoSetStore& blueprint, const CardAbstraction& card_abs,
                           const ActionAbstraction& action_abs)
    : blueprint_(blueprint), card_abs_(card_abs), action_abs_(action_abs) {}

Range RangeManager::build_opponent_range(
    int opponent_player, const std::vector<std::pair<GameState, Action>>& action_history,
    Card hero_c0, Card hero_c1, const Card* board, int num_board) const {
    // Start with uniform range, blocking hero cards and board cards
    CardMask dead = card_bit(hero_c0) | card_bit(hero_c1);
    for (int i = 0; i < num_board; ++i)
        dead |= card_bit(board[i]);

    Range range;
    range.init_uniform(dead);

    // Apply Bayesian filter for each opponent action in history
    for (const auto& [state, action] : action_history) {
        if (state.current_player() == opponent_player) {
            apply_action_filter(range, opponent_player, state, action);
        }
    }

    range.normalize();
    return range;
}

void RangeManager::apply_action_filter(Range& range, int player, const GameState& state_before,
                                       const Action& action_taken) const {
    // Get the abstract actions available at this state
    auto actions = action_abs_.get_actions(state_before);
    int num_actions = static_cast<int>(actions.size());
    if (num_actions == 0)
        return;

    // Find the index of the taken action (map to nearest abstract action)
    Action abstract_action = action_abs_.map_to_abstract(action_taken, state_before);
    int action_idx = -1;
    for (int a = 0; a < num_actions; ++a) {
        if (actions[a] == abstract_action) {
            action_idx = a;
            break;
        }
    }
    if (action_idx < 0) {
        // Action not found in abstraction — try exact match as fallback
        for (int a = 0; a < num_actions; ++a) {
            if (actions[a].type == action_taken.type) {
                action_idx = a;
                break;
            }
        }
    }
    if (action_idx < 0)
        return;  // Can't filter if action not in abstraction

    // Build dead card mask from board
    CardMask dead = 0;
    for (int i = 0; i < state_before.num_board_cards(); ++i)
        dead |= card_bit(state_before.board()[i]);

    Street street = state_before.street();
    int street_int = static_cast<int>(street);

    for (int i = 0; i < Range::NUM_COMBOS; ++i) {
        if (range.weights[i] == 0.0f)
            continue;

        Card c0, c1;
        Range::combo_from_index(i, c0, c1);

        // Skip combos blocked by board cards
        if ((dead & card_bit(c0)) || (dead & card_bit(c1))) {
            range.weights[i] = 0.0f;
            continue;
        }

        // Look up blueprint strategy for this combo
        Bucket bucket = card_abs_.get_bucket(street, c0, c1, state_before.board().data(),
                                             state_before.num_board_cards());
        InfoSetKey key =
            make_infoset_key(player, street_int, bucket, state_before.action_history_hash());

        const InfoSetData* data = blueprint_.find(key);
        float strategy[InfoSetData::MAX_ACTIONS];
        if (data && data->num_actions == static_cast<uint8_t>(num_actions)) {
            data->average_strategy(strategy);
        } else {
            // Fallback to uniform
            float uniform = 1.0f / static_cast<float>(num_actions);
            for (int a = 0; a < num_actions; ++a)
                strategy[a] = uniform;
        }

        // Weight combo by probability of taking this action
        range.weights[i] *= strategy[action_idx];
    }
}

}  // namespace poker
