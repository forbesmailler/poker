#include <gtest/gtest.h>
#include "card_abstraction.h"
#include "equity_calculator.h"
#include "action_abstraction.h"
#include "hand_evaluator.h"
#include "generated_config.h"
#include "rng.h"
#include <fstream>
#include <cstdio>
#include <map>

using namespace poker;

TEST(CardAbstraction, PreflopCanonical) {
    // Suited aces of different suits map to same bucket
    CardAbstraction abs;
    abs.build();

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
    abs.build();

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
    abs.build();

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

// --- Post-flop percentile bucketing tests ---

// Helper: build abstraction once, share across postflop tests
static CardAbstraction& get_built_abstraction() {
    static CardAbstraction abs;
    static bool initialized = false;
    if (!initialized) {
        abs.build(2);
        initialized = true;
    }
    return abs;
}

TEST(PostflopBucketing, RiverBucketOrdering) {
    auto& abs = get_built_abstraction();

    // Dry board: 2c 5d 9h Js 3c
    Card board[5] = {
        make_card(0, 0),   // 2c
        make_card(3, 1),   // 5d
        make_card(7, 2),   // 9h
        make_card(9, 3),   // Js
        make_card(1, 0),   // 3c
    };

    // AA (AhAd) should get a higher river bucket than 72o (7d 2h)
    auto aa_bucket = abs.get_bucket(Street::RIVER,
                                     make_card(12, 2), make_card(12, 1),
                                     board, 5);
    auto sevtwo_bucket = abs.get_bucket(Street::RIVER,
                                         make_card(5, 1), make_card(0, 2),
                                         board, 5);
    EXPECT_GT(aa_bucket, sevtwo_bucket);
}

TEST(PostflopBucketing, BucketRangeValid) {
    auto& abs = get_built_abstraction();
    Rng rng(42);

    // Test 1000 random hands per post-flop street
    Street streets[] = {Street::FLOP, Street::TURN, Street::RIVER};
    int num_board[] = {3, 4, 5};

    for (int si = 0; si < 3; ++si) {
        int nb = abs.num_buckets(streets[si]);
        for (int trial = 0; trial < 1000; ++trial) {
            // Deal random non-overlapping cards
            uint64_t used = 0;
            Card cards[7];
            int dealt = 0;
            while (dealt < 2 + num_board[si]) {
                Card c = static_cast<Card>(rng.next_u64() % 52);
                uint64_t bit = 1ULL << c;
                if (used & bit) continue;
                used |= bit;
                cards[dealt++] = c;
            }
            auto bucket = abs.get_bucket(streets[si], cards[0], cards[1],
                                          cards + 2, num_board[si]);
            EXPECT_GE(bucket, 0);
            EXPECT_LT(bucket, nb);
        }
    }
}

TEST(PostflopBucketing, BucketDeterminism) {
    auto& abs = get_built_abstraction();

    Card hole0 = make_card(10, 0);  // Qc
    Card hole1 = make_card(8, 1);   // Td
    Card board[5] = {
        make_card(2, 2),   // 4h
        make_card(6, 3),   // 8s
        make_card(11, 0),  // Kc
        make_card(4, 1),   // 6d
        make_card(0, 2),   // 2h
    };

    // Same inputs should always produce the same bucket
    for (int street_idx = 0; street_idx < 3; ++street_idx) {
        Street st = static_cast<Street>(street_idx + 1);
        int nb_cards = street_idx + 3;
        auto b1 = abs.get_bucket(st, hole0, hole1, board, nb_cards);
        auto b2 = abs.get_bucket(st, hole0, hole1, board, nb_cards);
        EXPECT_EQ(b1, b2);
    }
}

TEST(PostflopBucketing, BuildSaveLoadRoundTrip) {
    CardAbstraction abs1;
    abs1.build(2);

    const std::string path = "test_abstraction_roundtrip.bin";
    abs1.save(path);

    CardAbstraction abs2;
    abs2.load(path);
    ASSERT_TRUE(abs2.is_built());

    // Verify same buckets on several hands
    Rng rng(123);
    Street streets[] = {Street::PREFLOP, Street::FLOP, Street::TURN, Street::RIVER};
    int num_board[] = {0, 3, 4, 5};

    for (int si = 0; si < 4; ++si) {
        for (int trial = 0; trial < 100; ++trial) {
            uint64_t used = 0;
            Card cards[7];
            int dealt = 0;
            while (dealt < 2 + num_board[si]) {
                Card c = static_cast<Card>(rng.next_u64() % 52);
                uint64_t bit = 1ULL << c;
                if (used & bit) continue;
                used |= bit;
                cards[dealt++] = c;
            }
            auto b1 = abs1.get_bucket(streets[si], cards[0], cards[1],
                                       cards + 2, num_board[si]);
            auto b2 = abs2.get_bucket(streets[si], cards[0], cards[1],
                                       cards + 2, num_board[si]);
            EXPECT_EQ(b1, b2) << "Mismatch at street " << si << " trial " << trial;
        }
    }

    std::remove(path.c_str());
}

TEST(PostflopBucketing, BucketDistribution) {
    auto& abs = get_built_abstraction();
    Rng rng(99);

    // Over 10K random river hands, no bucket should have >5x the average count
    const int N = 10000;
    int nb = abs.num_buckets(Street::RIVER);
    std::map<Bucket, int> counts;

    for (int trial = 0; trial < N; ++trial) {
        uint64_t used = 0;
        Card cards[7];
        int dealt = 0;
        while (dealt < 7) {
            Card c = static_cast<Card>(rng.next_u64() % 52);
            uint64_t bit = 1ULL << c;
            if (used & bit) continue;
            used |= bit;
            cards[dealt++] = c;
        }
        auto bucket = abs.get_bucket(Street::RIVER, cards[0], cards[1],
                                      cards + 2, 5);
        counts[bucket]++;
    }

    double avg = static_cast<double>(N) / nb;
    for (auto& [b, cnt] : counts) {
        EXPECT_LT(cnt, avg * 5.0) << "Bucket " << b << " has " << cnt
                                   << " entries (avg=" << avg << ")";
    }
}
