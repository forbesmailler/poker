#include "subgame_cfr.h"
#include <cassert>
#include <cstring>
#include <cmath>

namespace poker {

SubgameCFR::SubgameCFR(const ActionAbstraction& action_abs, const HandEvaluator& eval,
                       const InfoSetStore* blueprint, const CardAbstraction* card_abs,
                       const ActionAbstraction* blueprint_action_abs)
    : action_abs_(action_abs),
      eval_(eval),
      blueprint_(blueprint),
      card_abs_(card_abs),
      blueprint_action_abs_(blueprint_action_abs) {}

double SubgameCFR::solve(const GameState& root, Card hero_c0, Card hero_c1, const Range& opp_range,
                         int hero_player, int opp_player, int num_iterations, bool depth_limited,
                         int num_equity_samples) {
    nodes_.clear();
    hero_player_ = hero_player;
    depth_limited_ = depth_limited;
    num_equity_samples_ = num_equity_samples;

    double ev = 0.0;
    for (int i = 0; i < num_iterations; ++i) {
        cfr_iteration_ = i;
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

    // Chance node: deal the next community card
    if (state.is_chance_node()) {
        bool is_turn_deal = (state.street() == Street::TURN && state.num_board_cards() < 4);

        // Depth-limited: at the turn chance node, estimate value via Monte Carlo equity
        if (depth_limited_ && is_turn_deal) {
            return estimate_equity_ev(state, hero_c0, hero_c1, opp_reach, hero_player, opp_player);
        }

        // Build dead card mask: hero cards + board cards already dealt
        CardMask dead = card_bit(hero_c0) | card_bit(hero_c1);
        for (int i = 0; i < state.num_board_cards(); ++i)
            dead |= card_bit(state.board()[i]);

        int num_cards_dealt = 0;
        double total_ev = 0.0;

        for (int c = 0; c < NUM_CARDS; ++c) {
            if (dead & card_bit(static_cast<Card>(c)))
                continue;

            // Deal this card (turn or river)
            GameState next = is_turn_deal ? state.deal_turn(static_cast<Card>(c))
                                          : state.deal_river(static_cast<Card>(c));

            // Filter opp_reach: zero out combos that use this card
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
            num_cards_dealt++;
        }

        // Average over all dealt cards (uniform chance probability)
        return (num_cards_dealt > 0) ? total_ev / num_cards_dealt : 0.0;
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

double SubgameCFR::estimate_equity_ev(const GameState& state, Card hero_c0, Card hero_c1,
                                      const Range& opp_reach, int hero_player, int opp_player) {
    // Build dead card mask: hero cards + board (3 flop cards)
    CardMask dead = card_bit(hero_c0) | card_bit(hero_c1);
    for (int i = 0; i < state.num_board_cards(); ++i)
        dead |= card_bit(state.board()[i]);

    // Collect live cards for sampling turn+river
    Card live_cards[NUM_CARDS];
    int num_live = 0;
    for (int c = 0; c < NUM_CARDS; ++c) {
        if (!(dead & card_bit(static_cast<Card>(c))))
            live_cards[num_live++] = static_cast<Card>(c);
    }

    // Deterministic but iteration-varying seed
    uint64_t seed = static_cast<uint64_t>(hero_c0) * 53 * 53 + static_cast<uint64_t>(hero_c1) * 53 +
                    static_cast<uint64_t>(cfr_iteration_) * 997 + state.action_history_hash();
    Rng rng(seed);

    bool use_blueprint =
        (blueprint_ != nullptr && card_abs_ != nullptr && blueprint_action_abs_ != nullptr);

    const auto& board = state.board();
    int pot = state.pot();
    int hero_invested = state.players()[hero_player].total_invested;
    double total_ev = 0.0;

    // Cache: turn buckets per turn card (avoid recomputing for repeated turn cards)
    // turn_bucket_cache[card][combo] — lazily populated
    // Using flat array: [NUM_CARDS * NUM_COMBOS], 0xFFFF = not computed
    std::vector<uint16_t> turn_bucket_cache;
    if (use_blueprint) {
        turn_bucket_cache.assign(static_cast<size_t>(NUM_CARDS) * Range::NUM_COMBOS, 0xFFFF);
    }

    for (int s = 0; s < num_equity_samples_; ++s) {
        // Sample turn + river from live cards (without replacement)
        int idx0 = rng.next_int(num_live);
        int idx1 = rng.next_int(num_live - 1);
        if (idx1 >= idx0)
            idx1++;

        Card turn_card = live_cards[idx0];
        Card river_card = live_cards[idx1];

        CardMask sample_dead = dead | card_bit(turn_card) | card_bit(river_card);

        if (use_blueprint) {
            GameState turn_state = state.deal_turn(turn_card);

            Card turn_board[4] = {board[0], board[1], board[2], turn_card};
            Card river_board[5] = {board[0], board[1], board[2], turn_card, river_card};

            // Lazily compute hero turn bucket (cached per turn card)
            int hero_combo = Range::combo_index(hero_c0, hero_c1);
            size_t hero_turn_idx = static_cast<size_t>(turn_card) * Range::NUM_COMBOS + hero_combo;
            if (turn_bucket_cache[hero_turn_idx] == 0xFFFF) {
                turn_bucket_cache[hero_turn_idx] =
                    card_abs_->get_bucket(Street::TURN, hero_c0, hero_c1, turn_board, 4);
            }
            uint16_t hero_turn_bucket = turn_bucket_cache[hero_turn_idx];

            // River buckets are cheap (hand rank table lookup), compute directly
            uint16_t hero_river_bucket =
                card_abs_->get_bucket(Street::RIVER, hero_c0, hero_c1, river_board, 5);

            for (int j = 0; j < Range::NUM_COMBOS; ++j) {
                if (opp_reach.weights[j] == 0.0f)
                    continue;

                Card oc0, oc1;
                Range::combo_from_index(j, oc0, oc1);

                if ((sample_dead & card_bit(oc0)) || (sample_dead & card_bit(oc1)))
                    continue;

                // Lazily compute opponent turn bucket (cached per turn card)
                size_t opp_turn_idx = static_cast<size_t>(turn_card) * Range::NUM_COMBOS + j;
                if (turn_bucket_cache[opp_turn_idx] == 0xFFFF) {
                    turn_bucket_cache[opp_turn_idx] =
                        card_abs_->get_bucket(Street::TURN, oc0, oc1, turn_board, 4);
                }
                uint16_t opp_turn_bucket = turn_bucket_cache[opp_turn_idx];

                uint16_t opp_river_bucket =
                    card_abs_->get_bucket(Street::RIVER, oc0, oc1, river_board, 5);

                uint16_t buckets[4] = {hero_turn_bucket, hero_river_bucket, opp_turn_bucket,
                                       opp_river_bucket};

                Rng combo_rng(seed + static_cast<uint64_t>(s) * 1327 + static_cast<uint64_t>(j));
                double payoff = blueprint_rollout(turn_state, hero_c0, hero_c1, oc0, oc1,
                                                  hero_player, opp_player, buckets, combo_rng);
                total_ev += opp_reach.weights[j] * payoff;
            }
        } else {
            // Raw equity fallback: compare hand ranks at showdown
            HandRank hero_rank = eval_.evaluate(hero_c0, hero_c1, board[0], board[1], board[2],
                                                turn_card, river_card);

            for (int j = 0; j < Range::NUM_COMBOS; ++j) {
                if (opp_reach.weights[j] == 0.0f)
                    continue;

                Card oc0, oc1;
                Range::combo_from_index(j, oc0, oc1);

                if ((sample_dead & card_bit(oc0)) || (sample_dead & card_bit(oc1)))
                    continue;

                HandRank opp_rank =
                    eval_.evaluate(oc0, oc1, board[0], board[1], board[2], turn_card, river_card);

                int cmp = HandEvaluator::compare(hero_rank, opp_rank);
                double payoff;
                if (cmp > 0) {
                    payoff = static_cast<double>(pot - hero_invested);
                } else if (cmp < 0) {
                    payoff = -static_cast<double>(hero_invested);
                } else {
                    payoff = static_cast<double>(pot) / 2.0 - static_cast<double>(hero_invested);
                }
                total_ev += opp_reach.weights[j] * payoff;
            }
        }
    }

    return total_ev / num_equity_samples_;
}

double SubgameCFR::blueprint_rollout(const GameState& state, Card hero_c0, Card hero_c1,
                                     Card opp_c0, Card opp_c1, int hero_player, int opp_player,
                                     const uint16_t buckets[4], Rng& rng) {
    // buckets: [0]=hero_turn, [1]=hero_river, [2]=opp_turn, [3]=opp_river
    // All precomputed by caller — no get_bucket() calls in this function.
    GameState s = state;

    // Simulate game using blueprint strategies until terminal
    for (int depth = 0; depth < 20; ++depth) {
        if (s.is_terminal()) {
            int pot = s.pot();
            int hero_invested = s.players()[hero_player].total_invested;

            bool hero_folded = s.players()[hero_player].status == PlayerStatus::FOLDED;
            bool opp_folded = s.players()[opp_player].status == PlayerStatus::FOLDED;

            if (hero_folded)
                return -static_cast<double>(hero_invested);
            if (opp_folded)
                return static_cast<double>(pot - hero_invested);

            // Showdown
            const auto& brd = s.board();
            HandRank hero_rank =
                eval_.evaluate(hero_c0, hero_c1, brd[0], brd[1], brd[2], brd[3], brd[4]);
            HandRank opp_rank =
                eval_.evaluate(opp_c0, opp_c1, brd[0], brd[1], brd[2], brd[3], brd[4]);

            int cmp = HandEvaluator::compare(hero_rank, opp_rank);
            if (cmp > 0)
                return static_cast<double>(pot - hero_invested);
            if (cmp < 0)
                return -static_cast<double>(hero_invested);
            return static_cast<double>(pot) / 2.0 - static_cast<double>(hero_invested);
        }

        if (s.is_chance_node()) {
            // Deal the river card — sample a random non-conflicting card
            CardMask chance_dead =
                card_bit(hero_c0) | card_bit(hero_c1) | card_bit(opp_c0) | card_bit(opp_c1);
            for (int i = 0; i < s.num_board_cards(); ++i)
                chance_dead |= card_bit(s.board()[i]);

            Card live[NUM_CARDS];
            int n_live = 0;
            for (int c = 0; c < NUM_CARDS; ++c) {
                if (!(chance_dead & card_bit(static_cast<Card>(c))))
                    live[n_live++] = static_cast<Card>(c);
            }
            if (n_live == 0)
                return 0.0;

            Card dealt = live[rng.next_int(n_live)];
            if (s.num_board_cards() < 4)
                s = s.deal_turn(dealt);
            else
                s = s.deal_river(dealt);
            continue;
        }

        // Decision node — look up blueprint strategy using precomputed bucket
        int acting = s.current_player();
        bool is_hero = (acting == hero_player);
        int street_int = static_cast<int>(s.street());

        // Select precomputed bucket: turn=index 0/2, river=index 1/3
        uint16_t bucket;
        if (s.street() == Street::TURN)
            bucket = is_hero ? buckets[0] : buckets[2];
        else
            bucket = is_hero ? buckets[1] : buckets[3];

        InfoSetKey key = make_infoset_key(acting, street_int, bucket, s.action_history_hash());

        auto actions = blueprint_action_abs_->get_actions(s);
        int num_actions = static_cast<int>(actions.size());
        if (num_actions == 0)
            return 0.0;

        float strategy[InfoSetData::MAX_ACTIONS];
        const InfoSetData* data = blueprint_->find(key);
        if (data && data->num_actions > 0) {
            data->average_strategy(strategy);
            if (data->num_actions != num_actions) {
                int n = std::min(static_cast<int>(data->num_actions), num_actions);
                float uniform = 1.0f / num_actions;
                for (int a = 0; a < num_actions; ++a)
                    strategy[a] = (a < n) ? strategy[a] : uniform;
            }
        } else {
            float uniform = 1.0f / num_actions;
            for (int a = 0; a < num_actions; ++a)
                strategy[a] = uniform;
        }

        int chosen = rng.sample_action(strategy, num_actions);
        s = s.apply_action(actions[chosen]);
    }

    return 0.0;
}

}  // namespace poker
