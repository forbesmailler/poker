#include "subgame_cfr.h"
#include <cassert>
#include <cstring>
#include <cmath>

namespace poker {

SubgameCFR::SubgameCFR(const ActionAbstraction& action_abs, const HandEvaluator& eval)
    : action_abs_(action_abs), eval_(eval) {}

double SubgameCFR::solve(const GameState& root, Card hero_c0, Card hero_c1, const Range& opp_range,
                         int hero_player, int opp_player, int num_iterations) {
    nodes_.clear();
    hero_player_ = hero_player;

    double ev = 0.0;
    for (int i = 0; i < num_iterations; ++i) {
        Range opp_reach = opp_range;
        ev = traverse(root, hero_c0, hero_c1, opp_reach, 1.0, hero_player, opp_player);
    }
    return ev;
}

void SubgameCFR::get_strategy(const GameState& root, Card hero_c0, Card hero_c1, float* out,
                              int num_actions) const {
    int combo = Range::combo_index(hero_c0, hero_c1);
    SubgameKey key = make_subgame_key(hero_player_, combo, root.action_history_hash());

    auto it = nodes_.find(key);
    if (it != nodes_.end() && it->second.num_actions > 0) {
        it->second.average_strategy(out);
        return;
    }
    // Fallback: uniform
    float uniform = 1.0f / static_cast<float>(num_actions);
    for (int a = 0; a < num_actions; ++a)
        out[a] = uniform;
}

double SubgameCFR::traverse(const GameState& state, Card hero_c0, Card hero_c1,
                            const Range& opp_reach, double hero_reach, int hero_player,
                            int opp_player) {
    if (state.is_terminal()) {
        return terminal_value(state, hero_c0, hero_c1, opp_reach, hero_player, opp_player);
    }

    // Chance node: deal the river card, enumerate all possibilities
    if (state.is_chance_node()) {
        // Build dead card mask: hero cards + board cards already dealt
        CardMask dead = card_bit(hero_c0) | card_bit(hero_c1);
        for (int i = 0; i < state.num_board_cards(); ++i)
            dead |= card_bit(state.board()[i]);

        int num_river_cards = 0;
        double total_ev = 0.0;

        for (int c = 0; c < NUM_CARDS; ++c) {
            if (dead & card_bit(static_cast<Card>(c)))
                continue;

            // Deal this river card
            GameState next = state.deal_river(static_cast<Card>(c));

            // Filter opp_reach: zero out combos that use this river card
            Range filtered = opp_reach;
            for (int j = 0; j < Range::NUM_COMBOS; ++j) {
                if (filtered.weights[j] == 0.0f)
                    continue;
                Card oc0, oc1;
                Range::combo_from_index(j, oc0, oc1);
                if (oc0 == c || oc1 == c)
                    filtered.weights[j] = 0.0f;
            }

            total_ev +=
                traverse(next, hero_c0, hero_c1, filtered, hero_reach, hero_player, opp_player);
            num_river_cards++;
        }

        // Average over all river cards (uniform chance probability)
        return (num_river_cards > 0) ? total_ev / num_river_cards : 0.0;
    }

    int acting_player = state.current_player();
    auto actions = action_abs_.get_actions(state);
    int num_actions = static_cast<int>(actions.size());
    if (num_actions == 0)
        return 0.0;

    if (acting_player == hero_player) {
        // ---- Hero node ----
        int combo = Range::combo_index(hero_c0, hero_c1);
        SubgameKey key = make_subgame_key(hero_player, combo, state.action_history_hash());

        auto& node = nodes_.try_emplace(key, num_actions).first->second;
        if (node.num_actions == 0)
            node.num_actions = static_cast<uint8_t>(num_actions);

        float strategy[SubgameNodeData::MAX_ACTIONS];
        node.current_strategy(strategy);

        double action_values[SubgameNodeData::MAX_ACTIONS] = {};
        double node_value = 0.0;

        for (int a = 0; a < num_actions; ++a) {
            GameState next = state.apply_action(actions[a]);
            action_values[a] = traverse(next, hero_c0, hero_c1, opp_reach, hero_reach * strategy[a],
                                        hero_player, opp_player);
            node_value += strategy[a] * action_values[a];
        }

        // Update regrets and strategy sums
        for (int a = 0; a < num_actions; ++a) {
            node.regret[a] += static_cast<float>(action_values[a] - node_value);
            node.strategy_sum[a] += static_cast<float>(hero_reach) * strategy[a];
        }

        return node_value;

    } else if (acting_player == opp_player) {
        // ---- Opponent node ----
        // For each live opponent combo, compute its strategy and split reach
        // into per-action sub-ranges, then recurse and update regrets.

        // Split opponent reach into per-action child ranges (stack-allocated)
        Range child_ranges[SubgameNodeData::MAX_ACTIONS];

        for (int j = 0; j < Range::NUM_COMBOS; ++j) {
            if (opp_reach.weights[j] == 0.0f)
                continue;

            Card oc0, oc1;
            Range::combo_from_index(j, oc0, oc1);

            // Skip combos conflicting with hero cards
            if (oc0 == hero_c0 || oc0 == hero_c1 || oc1 == hero_c0 || oc1 == hero_c1) {
                continue;
            }

            SubgameKey key = make_subgame_key(opp_player, j, state.action_history_hash());
            auto& node = nodes_.try_emplace(key, num_actions).first->second;
            if (node.num_actions == 0)
                node.num_actions = static_cast<uint8_t>(num_actions);

            float strategy[SubgameNodeData::MAX_ACTIONS];
            node.current_strategy(strategy);

            for (int a = 0; a < num_actions; ++a) {
                child_ranges[a].weights[j] = opp_reach.weights[j] * strategy[a];
            }
        }

        // Recurse on each action with the filtered range
        double action_values[SubgameNodeData::MAX_ACTIONS] = {};
        for (int a = 0; a < num_actions; ++a) {
            GameState next = state.apply_action(actions[a]);
            action_values[a] = traverse(next, hero_c0, hero_c1, child_ranges[a], hero_reach,
                                        hero_player, opp_player);
        }

        // Update opponent regrets per combo
        double total_ev = 0.0;
        for (int j = 0; j < Range::NUM_COMBOS; ++j) {
            if (opp_reach.weights[j] == 0.0f)
                continue;

            SubgameKey key = make_subgame_key(opp_player, j, state.action_history_hash());
            auto it = nodes_.find(key);
            if (it == nodes_.end())
                continue;

            auto& node = it->second;
            float strategy[SubgameNodeData::MAX_ACTIONS];
            node.current_strategy(strategy);

            // Compute per-combo expected value (from hero's perspective)
            double combo_node_value = 0.0;
            for (int a = 0; a < num_actions; ++a)
                combo_node_value += strategy[a] * action_values[a];

            // Opponent regrets are in opponent's utility (negative of hero's)
            for (int a = 0; a < num_actions; ++a) {
                double opp_regret = -(action_values[a] - combo_node_value);
                node.regret[a] += static_cast<float>(opp_regret);
                node.strategy_sum[a] += opp_reach.weights[j] * strategy[a];
            }

            total_ev += combo_node_value;
        }

        // Total EV is the sum of per-action EVs (already weighted by sub-ranges)
        double ev = 0.0;
        for (int a = 0; a < num_actions; ++a)
            ev += action_values[a];
        return ev;

    } else {
        // Non-hero, non-opponent — shouldn't happen in HU subgame
        return 0.0;
    }
}

double SubgameCFR::terminal_value(const GameState& state, Card hero_c0, Card hero_c1,
                                  const Range& opp_reach, int hero_player, int opp_player) const {
    int pot = state.pot();
    int hero_invested = state.players()[hero_player].total_invested;

    // Determine who folded
    bool hero_folded = state.players()[hero_player].status == PlayerStatus::FOLDED;
    bool opp_folded = state.players()[opp_player].status == PlayerStatus::FOLDED;

    if (hero_folded) {
        // Hero folded — loses invested chips
        double opp_total = 0.0;
        for (int j = 0; j < Range::NUM_COMBOS; ++j)
            opp_total += opp_reach.weights[j];
        return -static_cast<double>(hero_invested) * opp_total;
    }

    if (opp_folded) {
        // Opponent folded — hero wins the pot
        double opp_total = 0.0;
        for (int j = 0; j < Range::NUM_COMBOS; ++j)
            opp_total += opp_reach.weights[j];
        return static_cast<double>(pot - hero_invested) * opp_total;
    }

    // Showdown — evaluate hands
    const auto& board = state.board();
    HandRank hero_rank =
        eval_.evaluate(hero_c0, hero_c1, board[0], board[1], board[2], board[3], board[4]);

    CardMask dead = card_bit(hero_c0) | card_bit(hero_c1);
    for (int i = 0; i < 5; ++i)
        dead |= card_bit(board[i]);

    double ev = 0.0;
    for (int j = 0; j < Range::NUM_COMBOS; ++j) {
        if (opp_reach.weights[j] == 0.0f)
            continue;

        Card oc0, oc1;
        Range::combo_from_index(j, oc0, oc1);

        // Skip combos that conflict with hero or board
        if ((dead & card_bit(oc0)) || (dead & card_bit(oc1)))
            continue;

        HandRank opp_rank =
            eval_.evaluate(oc0, oc1, board[0], board[1], board[2], board[3], board[4]);

        int cmp = HandEvaluator::compare(hero_rank, opp_rank);
        double payoff;
        if (cmp > 0) {
            payoff = static_cast<double>(pot - hero_invested);
        } else if (cmp < 0) {
            payoff = -static_cast<double>(hero_invested);
        } else {
            payoff = static_cast<double>(pot) / 2.0 - static_cast<double>(hero_invested);
        }
        ev += opp_reach.weights[j] * payoff;
    }

    return ev;
}

}  // namespace poker
