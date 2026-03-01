#include <gtest/gtest.h>
#include "card_abstraction.h"
#include "equity_calculator.h"
#include "action_abstraction.h"
#include "hand_evaluator.h"
#include "generated_config.h"

using namespace poker;

TEST(CardAbstraction, PreflopCanonical) {
    // Suited aces of different suits map to same bucket
    CardAbstraction abs;
    abs.build_preflop_only();

    // AhKh and AsKs should have same bucket
    auto b1 = abs.get_bucket(Street::PREFLOP,
                              make_card(12, 2), make_card(11, 2),
                              nullptr, 0);
    auto b2 = abs.get_bucket(Street::PREFLOP,
                              make_card(12, 3), make_card(11, 3),
                              nullptr, 0);
    EXPECT_EQ(b1, b2);
}

TEST(CardAbstraction, PocketPairsSameBucket) {
    CardAbstraction abs;
    abs.build_preflop_only();

    // AcAd and AhAs should have same preflop bucket
    auto b1 = abs.get_bucket(Street::PREFLOP,
                              make_card(12, 0), make_card(12, 1),
                              nullptr, 0);
    auto b2 = abs.get_bucket(Street::PREFLOP,
                              make_card(12, 2), make_card(12, 3),
                              nullptr, 0);
    EXPECT_EQ(b1, b2);
}

TEST(CardAbstraction, DifferentHandsDifferentBuckets) {
    CardAbstraction abs;
    abs.build_preflop_only();

    // AA and 72o should have different buckets
    auto aa = abs.get_bucket(Street::PREFLOP,
                              make_card(12, 0), make_card(12, 1),
                              nullptr, 0);
    auto sevtwo = abs.get_bucket(Street::PREFLOP,
                                  make_card(5, 0), make_card(0, 1),
                                  nullptr, 0);
    EXPECT_NE(aa, sevtwo);
}

TEST(CardAbstraction, NumBuckets) {
    CardAbstraction abs;
    EXPECT_EQ(abs.num_buckets(Street::PREFLOP), config::PREFLOP_BUCKETS);
    EXPECT_EQ(abs.num_buckets(Street::FLOP), config::FLOP_BUCKETS);
    EXPECT_EQ(abs.num_buckets(Street::TURN), config::TURN_BUCKETS);
    EXPECT_EQ(abs.num_buckets(Street::RIVER), config::RIVER_BUCKETS);
}

TEST(EMD, IdenticalHistograms) {
    std::vector<float> a = {0.2f, 0.3f, 0.5f};
    std::vector<float> b = {0.2f, 0.3f, 0.5f};
    EXPECT_FLOAT_EQ(EquityCalculator::emd(a, b), 0.0f);
}

TEST(EMD, KnownDistance) {
    // Simple case: all mass at different locations
    std::vector<float> a = {1.0f, 0.0f, 0.0f};
    std::vector<float> b = {0.0f, 0.0f, 1.0f};
    // EMD: CDF_a = [1, 1, 1], CDF_b = [0, 0, 1]
    // |CDF_a - CDF_b| = [1, 1, 0], sum = 2
    EXPECT_FLOAT_EQ(EquityCalculator::emd(a, b), 2.0f);
}

TEST(EMD, Symmetry) {
    std::vector<float> a = {0.5f, 0.3f, 0.2f};
    std::vector<float> b = {0.1f, 0.2f, 0.7f};
    EXPECT_FLOAT_EQ(EquityCalculator::emd(a, b),
                    EquityCalculator::emd(b, a));
}

TEST(EquityCalculator, PocketAcesEquity) {
    const HandEvaluator& eval = get_evaluator();
    EquityCalculator calc;

    // AA vs random hand preflop equity should be ~85%
    float equity = calc.compute_equity(
        make_card(12, 0), make_card(12, 1),
        nullptr, 0, eval, 5000);

    EXPECT_GT(equity, 0.75f);
    EXPECT_LT(equity, 0.95f);
}

TEST(EquityCalculator, LowHandEquity) {
    const HandEvaluator& eval = get_evaluator();
    EquityCalculator calc;

    // 72o vs random — should be low
    float equity = calc.compute_equity(
        make_card(5, 0), make_card(0, 1),
        nullptr, 0, eval, 5000);

    EXPECT_LT(equity, 0.45f);
}

TEST(ActionAbstraction, AlwaysHasFoldOrCheck) {
    ActionAbstraction abs;
    std::array<int32_t, MAX_PLAYERS> stacks;
    stacks.fill(200);
    auto state = GameState::new_hand(stacks, 0, 1, 2);

    auto actions = abs.get_actions(state);
    EXPECT_FALSE(actions.empty());

    // Should have fold or check
    bool has_fold_or_check = false;
    for (const auto& a : actions) {
        if (a.type == ActionType::FOLD || a.type == ActionType::CHECK) {
            has_fold_or_check = true;
        }
    }
    EXPECT_TRUE(has_fold_or_check);
}

TEST(ActionAbstraction, HasRaiseOptions) {
    ActionAbstraction abs;
    std::array<int32_t, MAX_PLAYERS> stacks;
    stacks.fill(200);
    auto state = GameState::new_hand(stacks, 0, 1, 2);

    auto actions = abs.get_actions(state);

    // Should have at least one bet/raise option
    bool has_bet = false;
    for (const auto& a : actions) {
        if (a.type == ActionType::BET) has_bet = true;
    }
    EXPECT_TRUE(has_bet);
}
