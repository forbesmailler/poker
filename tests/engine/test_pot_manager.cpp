#include <gtest/gtest.h>
#include "pot_manager.h"

using namespace poker;

TEST(PotManager, SimpleHeadsUpPot) {
    PotManager pm;
    pm.post_blind(0, 1);  // SB
    pm.post_blind(1, 2);  // BB

    // Player 0 calls
    pm.add_bet(0, 1);  // Now both have 2

    pm.finalize_round();

    EXPECT_EQ(pm.total(), 4);
}

TEST(PotManager, FoldGivesPotToWinner) {
    PotManager pm;
    pm.post_blind(0, 1);
    pm.post_blind(1, 2);

    pm.player_folds(0);  // SB folds
    pm.finalize_round();

    // Resolve: player 1 wins
    std::array<uint16_t, MAX_PLAYERS_CONST> ranks = {};
    ranks[1] = 100;
    std::bitset<MAX_PLAYERS_CONST> active;
    active.set(1);

    auto winnings = pm.resolve(ranks, active);
    EXPECT_EQ(winnings[1], 3);  // Gets all chips in pot
}

TEST(PotManager, ThreeWayAllInDifferentStacks) {
    PotManager pm;

    // Player 0: 50 chips all-in
    // Player 1: 100 chips all-in
    // Player 2: 200 chips all-in
    pm.add_bet(0, 50);
    pm.add_bet(1, 100);
    pm.add_bet(2, 100);  // Calls 100, doesn't go all-in yet

    pm.finalize_round();

    // Main pot: 3 * 50 = 150 (all three eligible)
    // Side pot: 2 * 50 = 100 (players 1 and 2 eligible)
    int total = pm.total();
    EXPECT_EQ(total, 250);

    // If player 0 has best hand
    {
        std::array<uint16_t, MAX_PLAYERS_CONST> ranks = {};
        ranks[0] = 300;  // Best
        ranks[1] = 200;
        ranks[2] = 100;
        std::bitset<MAX_PLAYERS_CONST> active;
        active.set(0); active.set(1); active.set(2);

        auto winnings = pm.resolve(ranks, active);
        EXPECT_EQ(winnings[0], 150);  // Main pot
        EXPECT_EQ(winnings[1], 100);  // Side pot
        EXPECT_EQ(winnings[2], 0);
    }
}

TEST(PotManager, SplitPot) {
    PotManager pm;
    pm.add_bet(0, 100);
    pm.add_bet(1, 100);
    pm.finalize_round();

    // Same hand rank = split
    std::array<uint16_t, MAX_PLAYERS_CONST> ranks = {};
    ranks[0] = 500;
    ranks[1] = 500;
    std::bitset<MAX_PLAYERS_CONST> active;
    active.set(0); active.set(1);

    auto winnings = pm.resolve(ranks, active);
    EXPECT_EQ(winnings[0] + winnings[1], 200);
    EXPECT_EQ(winnings[0], 100);
    EXPECT_EQ(winnings[1], 100);
}

TEST(PotManager, MultipleRounds) {
    PotManager pm;

    // Preflop: both put in 10
    pm.add_bet(0, 10);
    pm.add_bet(1, 10);
    pm.finalize_round();

    // Flop: both put in 20
    pm.add_bet(0, 20);
    pm.add_bet(1, 20);
    pm.finalize_round();

    EXPECT_EQ(pm.total(), 60);
}
