#include <gtest/gtest.h>
#include "card_abstraction.h"
#include "hand_evaluator.h"
#include "generated_config.h"
#include "rng.h"
#include <cstdio>
#include <map>

using namespace poker;

// Build abstraction once with sampled enumeration (~500K combos/street).
// Shared across all tests in this binary via single CTest entry.
static CardAbstraction& get_built_abstraction() {
    static CardAbstraction abs;
    static bool initialized = false;
    if (!initialized) {
        abs.build(4, 1000);
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

    // Test random hands per post-flop street
    Street streets[] = {Street::FLOP, Street::TURN, Street::RIVER};
    int num_board[] = {3, 4, 5};

    for (int si = 0; si < 3; ++si) {
        int nb = abs.num_buckets(streets[si]);
        for (int trial = 0; trial < 50; ++trial) {
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
    auto& abs = get_built_abstraction();

    const std::string path = "test_abstraction_roundtrip.bin";
    abs.save(path);

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
            auto b1 = abs.get_bucket(streets[si], cards[0], cards[1],
                                      cards + 2, num_board[si]);
            auto b2 = abs2.get_bucket(streets[si], cards[0], cards[1],
                                       cards + 2, num_board[si]);
            EXPECT_EQ(b1, b2) << "Mismatch at street " << si << " trial " << trial;
        }
    }

    std::remove(path.c_str());
}

TEST(PostflopBucketing, FlopRolloutCapturesDraws) {
    auto& abs = get_built_abstraction();

    // Board: Ah 5h 9h  (three hearts = flush draw board)
    Card board[3] = {
        make_card(12, 2),  // Ah
        make_card(3, 2),   // 5h
        make_card(7, 2),   // 9h
    };

    // Kh Qh — flush draw (two more hearts, will hit flush often)
    auto flush_draw = abs.get_bucket(Street::FLOP,
                                      make_card(11, 2),  // Kh
                                      make_card(10, 2),  // Qh
                                      board, 3);

    // 2c 3d — no draw, no pair, weak
    auto weak = abs.get_bucket(Street::FLOP,
                                make_card(0, 0),   // 2c
                                make_card(1, 1),   // 3d
                                board, 3);

    EXPECT_GT(flush_draw, weak);
}

TEST(PostflopBucketing, TurnRolloutCapturesDraws) {
    auto& abs = get_built_abstraction();

    // Board: 7h 8d 9c Ts  (four to a straight)
    Card board[4] = {
        make_card(5, 2),   // 7h
        make_card(6, 1),   // 8d
        make_card(7, 0),   // 9c
        make_card(8, 3),   // Ts
    };

    // JQ should get a high turn bucket — averaging over 46 river cards,
    // many complete the straight (already made straight with 7-8-9-T-J)
    auto draw_bucket = abs.get_bucket(Street::TURN,
                                       make_card(9, 0),   // Jc
                                       make_card(10, 1),  // Qd
                                       board, 4);

    // 2c 3d — no draw potential, weak hand
    auto weak_bucket = abs.get_bucket(Street::TURN,
                                       make_card(0, 0),   // 2c
                                       make_card(1, 1),   // 3d
                                       board, 4);

    EXPECT_GT(draw_bucket, weak_bucket);
}

TEST(PostflopBucketing, TurnBucketDistribution) {
    auto& abs = get_built_abstraction();
    Rng rng(77);

    // Over 200 random turn hands, verify buckets are reasonably covered
    const int N = 200;
    int nb = abs.num_buckets(Street::TURN);
    std::map<Bucket, int> counts;

    for (int trial = 0; trial < N; ++trial) {
        uint64_t used = 0;
        Card cards[6];
        int dealt = 0;
        while (dealt < 6) {
            Card c = static_cast<Card>(rng.next_u64() % 52);
            uint64_t bit = 1ULL << c;
            if (used & bit) continue;
            used |= bit;
            cards[dealt++] = c;
        }
        auto bucket = abs.get_bucket(Street::TURN, cards[0], cards[1],
                                      cards + 2, 4);
        counts[bucket]++;
    }

    // With fewer samples, check at least 1/5 of buckets are populated
    EXPECT_GT(static_cast<int>(counts.size()), nb / 5)
        << "Only " << counts.size() << "/" << nb << " buckets populated";
}

TEST(PostflopBucketing, RiverBucketDistribution) {
    auto& abs = get_built_abstraction();
    Rng rng(99);

    const int N = 500;
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

    // With fewer samples, check at least 1/5 of buckets are populated
    EXPECT_GT(static_cast<int>(counts.size()), nb / 5)
        << "Only " << counts.size() << "/" << nb << " buckets populated";
}
