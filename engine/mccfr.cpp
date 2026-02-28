#include "mccfr.h"
#include "deck.h"
#include <cassert>
#include <cstring>

namespace poker {

MCCFR::MCCFR(InfoSetStore& store, const CardAbstraction& card_abs,
             const ActionAbstraction& action_abs, const HandEvaluator& eval)
    : store_(store), card_abs_(card_abs), action_abs_(action_abs), eval_(eval) {}

double MCCFR::traverse(const GameState& state, int traversing_player, Rng& rng, int iteration) {
    // Terminal node: return payoff
    if (state.is_terminal()) {
        auto payoffs = state.payoffs(eval_);
        return payoffs[traversing_player];
    }

    // Chance node: sample outcome
    if (state.is_chance_node()) {
        GameState next = sample_chance(state, rng);
        return traverse(next, traversing_player, rng, iteration);
    }

    int player = state.current_player();
    auto actions = action_abs_.get_actions(state);
    int num_actions = static_cast<int>(actions.size());

    if (num_actions == 0) {
        // No legal actions — shouldn't happen normally
        return 0.0;
    }

    InfoSetKey key = compute_key(state, player);
    InfoSetData& infoset = store_.get_or_create(key, num_actions);

    float strategy[InfoSetData::MAX_ACTIONS];
    infoset.current_strategy(strategy);

    if (player == traversing_player) {
        // Explore ALL actions for the traverser
        double action_values[InfoSetData::MAX_ACTIONS];
        std::memset(action_values, 0, sizeof(action_values));

        for (int a = 0; a < num_actions; ++a) {
            GameState next = state.apply_action(actions[a]);
            action_values[a] = traverse(next, traversing_player, rng, iteration);
        }

        // Compute expected value under current strategy
        double node_value = 0.0;
        for (int a = 0; a < num_actions; ++a) {
            node_value += strategy[a] * action_values[a];
        }

        // Update cumulative regrets
        for (int a = 0; a < num_actions; ++a) {
            double regret = action_values[a] - node_value;
            infoset.cumulative_regret[a] += static_cast<float>(regret);
        }

        return node_value;
    } else {
        // Sample ONE action for opponent
        int a = rng.sample_action(strategy, num_actions);
        GameState next = state.apply_action(actions[a]);

        // Update strategy sum (for average strategy computation)
        for (int i = 0; i < num_actions; ++i) {
            infoset.strategy_sum[i] += strategy[i];
        }

        return traverse(next, traversing_player, rng, iteration);
    }
}

GameState MCCFR::sample_chance(const GameState& state, Rng& rng) {
    // Deal community cards based on current street
    GameState next = state;
    Deck deck;

    // Remove known cards from deck
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (state.players()[i].hole_cards[0] != CARD_NONE) {
            deck.remove(state.players()[i].hole_cards[0]);
            deck.remove(state.players()[i].hole_cards[1]);
        }
    }
    for (int i = 0; i < state.num_board_cards(); ++i) {
        deck.remove(state.board()[i]);
    }

    deck.shuffle(rng);

    switch (state.street()) {
        case Street::FLOP:
            if (state.num_board_cards() == 0) {
                Card c0 = deck.deal(), c1 = deck.deal(), c2 = deck.deal();
                next = next.deal_flop(c0, c1, c2);
            }
            break;
        case Street::TURN:
            if (state.num_board_cards() == 3) {
                Card c = deck.deal();
                next = next.deal_turn(c);
            }
            break;
        case Street::RIVER:
            if (state.num_board_cards() == 4) {
                Card c = deck.deal();
                next = next.deal_river(c);
            }
            break;
        default:
            break;
    }

    return next;
}

InfoSetKey MCCFR::compute_key(const GameState& state, int player) const {
    uint16_t bucket = card_abs_.get_bucket(state.street(), state.players()[player].hole_cards[0],
                                           state.players()[player].hole_cards[1],
                                           state.board().data(), state.num_board_cards());

    return make_infoset_key(player, static_cast<int>(state.street()), bucket,
                            state.action_history_hash());
}

}  // namespace poker
