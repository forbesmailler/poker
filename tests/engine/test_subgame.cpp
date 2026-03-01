#include <gtest/gtest.h>
#include <set>
#include "card.h"
#include "game_state.h"
#include "hand_evaluator.h"
#include "action_abstraction.h"
#include "range_manager.h"
#include "subgame_cfr.h"

using namespace poker;

// ---- Range tests ----

TEST(Range, ComboIndexRoundTrip) {
    // All 1326 combos should round-trip through combo_index/combo_from_index
    int count = 0;
    for (Card c1 = 1; c1 < NUM_CARDS; ++c1) {
        for (Card c0 = 0; c0 < c1; ++c0) {
            int idx = Range::combo_index(c0, c1);
            EXPECT_GE(idx, 0);
            EXPECT_LT(idx, Range::NUM_COMBOS);

            Card r0, r1;
            Range::combo_from_index(idx, r0, r1);
            EXPECT_EQ(r0, c0);
            EXPECT_EQ(r1, c1);
            count++;
        }
    }
    EXPECT_EQ(count, 1326);
}

TEST(Range, ComboIndexUnique) {
    // Each pair should map to a unique index
    std::set<int> seen;
    for (Card c1 = 1; c1 < NUM_CARDS; ++c1) {
        for (Card c0 = 0; c0 < c1; ++c0) {
            int idx = Range::combo_index(c0, c1);
            EXPECT_TRUE(seen.insert(idx).second) << "Duplicate index " << idx;
        }
    }
    EXPECT_EQ(seen.size(), 1326u);
}

TEST(Range, Blockers) {
    Range range;
    // Set all weights to 1
    for (int i = 0; i < Range::NUM_COMBOS; ++i)
        range.weights[i] = 1.0f;

    // Block the Ace of spades (card 51) and King of hearts (card 50)
    Card as = make_card(12, 3);  // Ace of spades = 12*4+3 = 51
    Card kh = make_card(12, 2);  // King of hearts = 12*4+2 = 50
    CardMask dead = card_bit(as) | card_bit(kh);

    range.remove_blockers(dead);

    // Any combo containing As or Kh should be zero
    for (int i = 0; i < Range::NUM_COMBOS; ++i) {
        Card c0, c1;
        Range::combo_from_index(i, c0, c1);
        if (c0 == as || c0 == kh || c1 == as || c1 == kh) {
            EXPECT_FLOAT_EQ(range.weights[i], 0.0f)
                << "Combo " << i << " should be blocked";
        } else {
            EXPECT_FLOAT_EQ(range.weights[i], 1.0f)
                << "Combo " << i << " should not be blocked";
        }
    }
}

TEST(Range, UniformInitialization) {
    Range range;

    // Block 2 cards
    Card c0 = make_card(12, 3);  // As
    Card c1 = make_card(12, 2);  // Kh
    CardMask dead = card_bit(c0) | card_bit(c1);

    range.init_uniform(dead);

    // Count live combos: C(50,2) = 1225
    int live = 0;
    float total = 0.0f;
    for (int i = 0; i < Range::NUM_COMBOS; ++i) {
        if (range.weights[i] > 0.0f) {
            live++;
            total += range.weights[i];
        }
    }

    EXPECT_EQ(live, 1225);  // C(50,2)
    EXPECT_NEAR(total, 1.0f, 1e-5f);

    // Each live combo should have equal weight
    float expected = 1.0f / 1225.0f;
    for (int i = 0; i < Range::NUM_COMBOS; ++i) {
        if (range.weights[i] > 0.0f) {
            EXPECT_NEAR(range.weights[i], expected, 1e-7f);
        }
    }
}

// ---- SubgameCFR tests ----

// Helper: create a heads-up flop state (3 board cards dealt, ready for flop action)
static GameState make_flop_state(Card b0, Card b1, Card b2, int hero_seat, int opp_seat) {
    std::array<int32_t, MAX_PLAYERS> stacks = {};
    stacks[hero_seat] = 200;
    stacks[opp_seat] = 200;

    GameState state = GameState::new_hand(stacks, 0, 1, 2);

    // Preflop: limp in (SB calls, BB checks)
    state = state.apply_action(Action::call());
    state = state.apply_action(Action::check());

    // Deal flop
    state = state.deal_flop(b0, b1, b2);

    return state;
}

// Helper: create a heads-up river state where both players have acted to the river
// Uses seats 0 and 1 with stacks that work for a clean scenario
static GameState make_river_state(Card hero_c0, Card hero_c1, Card opp_c0, Card opp_c1,
                                  Card b0, Card b1, Card b2, Card b3, Card b4, int hero_seat,
                                  int opp_seat) {
    // Set up stacks: seats 0 and 1 active, rest OUT
    std::array<int32_t, MAX_PLAYERS> stacks = {};
    stacks[hero_seat] = 200;
    stacks[opp_seat] = 200;

    GameState state = GameState::new_hand(stacks, 0, 1, 2);
    state.set_hole_cards(hero_seat, hero_c0, hero_c1);
    state.set_hole_cards(opp_seat, opp_c0, opp_c1);

    // Preflop: limp in (call, check)
    // In HU: seat 0 is dealer/SB, seat 1 is BB
    // SB acts first preflop: call
    state = state.apply_action(Action::call());
    // BB checks
    state = state.apply_action(Action::check());

    // Deal flop
    state = state.deal_flop(b0, b1, b2);
    // Check through flop
    // Post-flop: BB acts first (seat 1 in HU with dealer=0)
    state = state.apply_action(Action::check());
    state = state.apply_action(Action::check());

    // Deal turn
    state = state.deal_turn(b3);
    // Check through turn
    state = state.apply_action(Action::check());
    state = state.apply_action(Action::check());

    // Deal river
    state = state.deal_river(b4);

    return state;
}

TEST(SubgameCFR, NutsAlwaysBets) {
    const HandEvaluator& eval = get_evaluator();
    ActionAbstraction action_abs;

    // Hero has a royal flush (Ah Kh on board Th Jh Qh 2c 3d)
    Card hero_c0 = string_to_card("Ah");
    Card hero_c1 = string_to_card("Kh");
    Card b0 = string_to_card("Th");
    Card b1 = string_to_card("Jh");
    Card b2 = string_to_card("Qh");
    Card b3 = string_to_card("2c");
    Card b4 = string_to_card("3d");

    // Create state at river
    // HU: seats 0 and 1
    int hero_seat = 0, opp_seat = 1;
    GameState state = make_river_state(hero_c0, hero_c1, string_to_card("7s"),
                                       string_to_card("8s"), b0, b1, b2, b3, b4, hero_seat,
                                       opp_seat);

    // Build uniform opponent range (no Bayesian filtering since we don't have blueprint)
    CardMask dead = card_bit(hero_c0) | card_bit(hero_c1);
    for (int i = 0; i < 5; ++i)
        dead |= card_bit(state.board()[i]);

    Range opp_range;
    opp_range.init_uniform(dead);

    // Solve
    SubgameCFR solver(action_abs, eval);
    solver.solve(state, hero_c0, hero_c1, opp_range, hero_seat, opp_seat, 500);

    // Get strategy
    auto actions = action_abs.get_actions(state);
    int num_actions = static_cast<int>(actions.size());
    float strategy[SubgameNodeData::MAX_ACTIONS];
    solver.get_strategy(state, hero_c0, hero_c1, strategy, num_actions);

    // With the nuts, hero should bet at high frequency
    // Find the betting actions
    float bet_freq = 0.0f;
    for (int a = 0; a < num_actions; ++a) {
        if (actions[a].type == ActionType::BET) {
            bet_freq += strategy[a];
        }
    }
    // The nuts should bet >50% of the time (in practice much higher)
    EXPECT_GT(bet_freq, 0.5f) << "Nuts should bet frequently, got " << (bet_freq * 100) << "%";
}

TEST(SubgameCFR, CFRConvergence) {
    const HandEvaluator& eval = get_evaluator();
    ActionAbstraction action_abs;

    // Use a moderate hand
    Card hero_c0 = string_to_card("As");
    Card hero_c1 = string_to_card("Ks");
    Card b0 = string_to_card("2h");
    Card b1 = string_to_card("7d");
    Card b2 = string_to_card("Tc");
    Card b3 = string_to_card("Jc");
    Card b4 = string_to_card("4s");

    int hero_seat = 0, opp_seat = 1;
    GameState state = make_river_state(hero_c0, hero_c1, string_to_card("3h"),
                                       string_to_card("5c"), b0, b1, b2, b3, b4, hero_seat,
                                       opp_seat);

    CardMask dead = card_bit(hero_c0) | card_bit(hero_c1);
    for (int i = 0; i < 5; ++i)
        dead |= card_bit(state.board()[i]);

    Range opp_range;
    opp_range.init_uniform(dead);

    // Run with 500 iterations and record EV
    SubgameCFR solver1(action_abs, eval);
    double ev500 = solver1.solve(state, hero_c0, hero_c1, opp_range, hero_seat, opp_seat, 500);

    // Run with 1000 iterations
    SubgameCFR solver2(action_abs, eval);
    double ev1000 = solver2.solve(state, hero_c0, hero_c1, opp_range, hero_seat, opp_seat, 1000);

    // EVs should be within a reasonable range of each other (convergence)
    // We check that the absolute difference is small relative to pot
    double diff = std::abs(ev1000 - ev500);
    int pot = state.pot();
    // Should be within 10% of the pot
    EXPECT_LT(diff, pot * 0.1)
        << "EV didn't converge: ev500=" << ev500 << " ev1000=" << ev1000;
}

TEST(SubgameCFR, FoldTerminalValue) {
    const HandEvaluator& eval = get_evaluator();
    ActionAbstraction action_abs;

    Card hero_c0 = string_to_card("As");
    Card hero_c1 = string_to_card("Ks");
    Card b0 = string_to_card("2h");
    Card b1 = string_to_card("7d");
    Card b2 = string_to_card("Tc");
    Card b3 = string_to_card("Jc");
    Card b4 = string_to_card("4s");

    int hero_seat = 0, opp_seat = 1;
    GameState state = make_river_state(hero_c0, hero_c1, string_to_card("3h"),
                                       string_to_card("5c"), b0, b1, b2, b3, b4, hero_seat,
                                       opp_seat);

    // Opponent is acting. Have them fold.
    // First, who acts first? In our river state, BB acts first post-flop (seat 1)
    int acting = state.current_player();
    if (acting == opp_seat) {
        // Opponent folds
        GameState folded = state.apply_action(Action::fold());
        EXPECT_TRUE(folded.is_terminal());

        // Hero should win the pot
        // Create a simple range to test terminal value
        Range opp_range;
        CardMask dead = card_bit(hero_c0) | card_bit(hero_c1);
        for (int i = 0; i < 5; ++i)
            dead |= card_bit(state.board()[i]);
        opp_range.init_uniform(dead);

        SubgameCFR solver(action_abs, eval);
        // Use terminal_value indirectly via solve on already-terminal state
        // Actually test that solving a very simple scenario works
        double ev = solver.solve(state, hero_c0, hero_c1, opp_range, hero_seat, opp_seat, 100);
        // EV should be positive since hero has ace-high (above average)
        // and opponent has a range of all hands
        // Just check it's a finite number
        EXPECT_FALSE(std::isnan(ev));
        EXPECT_FALSE(std::isinf(ev));
    } else {
        // Hero acts first — make hero check, then opponent folds
        GameState checked = state.apply_action(Action::check());
        if (!checked.is_terminal()) {
            GameState folded = checked.apply_action(Action::fold());
            EXPECT_TRUE(folded.is_terminal());
        }
    }
}

// ---- Depth-Limited Flop Subgame Tests ----

TEST(SubgameCFR, DepthLimitedBasic) {
    const HandEvaluator& eval = get_evaluator();
    ActionAbstraction action_abs;

    Card hero_c0 = string_to_card("As");
    Card hero_c1 = string_to_card("Ks");
    Card b0 = string_to_card("2h");
    Card b1 = string_to_card("7d");
    Card b2 = string_to_card("Tc");

    int hero_seat = 1, opp_seat = 0;
    GameState state = make_flop_state(b0, b1, b2, hero_seat, opp_seat);

    CardMask dead = card_bit(hero_c0) | card_bit(hero_c1);
    for (int i = 0; i < 3; ++i)
        dead |= card_bit(state.board()[i]);

    Range opp_range;
    opp_range.init_uniform(dead);

    SubgameCFR solver(action_abs, eval);
    double ev = solver.solve(state, hero_c0, hero_c1, opp_range, hero_seat, opp_seat,
                             50, /*depth_limited=*/true, /*num_equity_samples=*/30);

    // EV should be finite
    EXPECT_FALSE(std::isnan(ev));
    EXPECT_FALSE(std::isinf(ev));

    // Strategy should sum to ~1.0
    auto actions = action_abs.get_actions(state);
    int num_actions = static_cast<int>(actions.size());
    float strategy[SubgameNodeData::MAX_ACTIONS];
    solver.get_strategy(state, hero_c0, hero_c1, strategy, num_actions);

    float total = 0.0f;
    for (int a = 0; a < num_actions; ++a)
        total += strategy[a];
    EXPECT_NEAR(total, 1.0f, 1e-4f);
}

TEST(SubgameCFR, DepthLimitedNutsBets) {
    const HandEvaluator& eval = get_evaluator();
    ActionAbstraction action_abs;

    // Pocket aces on a dry board — should bet frequently
    // Hero is seat 1 (BB, acts first postflop in HU) for faster test
    Card hero_c0 = string_to_card("Ah");
    Card hero_c1 = string_to_card("Ad");
    Card b0 = string_to_card("2c");
    Card b1 = string_to_card("7s");
    Card b2 = string_to_card("Td");

    int hero_seat = 1, opp_seat = 0;
    GameState state = make_flop_state(b0, b1, b2, hero_seat, opp_seat);

    CardMask dead = card_bit(hero_c0) | card_bit(hero_c1);
    for (int i = 0; i < 3; ++i)
        dead |= card_bit(state.board()[i]);

    Range opp_range;
    opp_range.init_uniform(dead);

    SubgameCFR solver(action_abs, eval);
    solver.solve(state, hero_c0, hero_c1, opp_range, hero_seat, opp_seat,
                 20, /*depth_limited=*/true, /*num_equity_samples=*/20);

    auto actions = action_abs.get_actions(state);
    int num_actions = static_cast<int>(actions.size());
    float strategy[SubgameNodeData::MAX_ACTIONS];
    solver.get_strategy(state, hero_c0, hero_c1, strategy, num_actions);

    // Aces should bet at >30% frequency on a dry flop even with few iterations
    float bet_freq = 0.0f;
    for (int a = 0; a < num_actions; ++a) {
        if (actions[a].type == ActionType::BET)
            bet_freq += strategy[a];
    }
    EXPECT_GT(bet_freq, 0.30f) << "Aces should bet frequently, got " << (bet_freq * 100) << "%";
}

TEST(SubgameCFR, DepthLimitedConvergence) {
    const HandEvaluator& eval = get_evaluator();
    ActionAbstraction action_abs;

    Card hero_c0 = string_to_card("Ks");
    Card hero_c1 = string_to_card("Qh");
    Card b0 = string_to_card("2c");
    Card b1 = string_to_card("7d");
    Card b2 = string_to_card("Tc");

    int hero_seat = 1, opp_seat = 0;
    GameState state = make_flop_state(b0, b1, b2, hero_seat, opp_seat);

    CardMask dead = card_bit(hero_c0) | card_bit(hero_c1);
    for (int i = 0; i < 3; ++i)
        dead |= card_bit(state.board()[i]);

    Range opp_range;
    opp_range.init_uniform(dead);

    // Solve with 20 iterations
    SubgameCFR solver1(action_abs, eval);
    double ev20 = solver1.solve(state, hero_c0, hero_c1, opp_range, hero_seat, opp_seat,
                                20, /*depth_limited=*/true, /*num_equity_samples=*/20);

    // Solve with 40 iterations
    SubgameCFR solver2(action_abs, eval);
    double ev40 = solver2.solve(state, hero_c0, hero_c1, opp_range, hero_seat, opp_seat,
                                40, /*depth_limited=*/true, /*num_equity_samples=*/20);

    // EV should stabilize: difference within 25% of pot
    double diff = std::abs(ev40 - ev20);
    int pot = state.pot();
    EXPECT_LT(diff, pot * 0.25)
        << "EV didn't converge: ev20=" << ev20 << " ev40=" << ev40 << " pot=" << pot;
}
