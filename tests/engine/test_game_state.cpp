#include <gtest/gtest.h>
#include "game_state.h"
#include "hand_evaluator.h"

using namespace poker;

class GameStateTest : public ::testing::Test {
protected:
    std::array<int32_t, MAX_PLAYERS> stacks;
    std::vector<BetSize> bet_sizes;

    void SetUp() override {
        stacks.fill(200);
        bet_sizes = {{0.5f, false}, {1.0f, false}, {0.0f, true}};
    }
};

TEST_F(GameStateTest, BlindPosting) {
    auto state = GameState::new_hand(stacks, 0, 1, 2);
    EXPECT_EQ(state.pot(), 3);  // SB (1) + BB (2)
    EXPECT_EQ(state.street(), Street::PREFLOP);
}

TEST_F(GameStateTest, InitialPlayerStacks) {
    auto state = GameState::new_hand(stacks, 0, 1, 2);
    const auto& players = state.players();
    // SB (player 1) should have 199
    EXPECT_EQ(players[1].stack, 199);
    // BB (player 2) should have 198
    EXPECT_EQ(players[2].stack, 198);
    // Others unchanged
    EXPECT_EQ(players[3].stack, 200);
}

TEST_F(GameStateTest, FirstToActPreflop) {
    // 6-max: dealer=0, SB=1, BB=2, UTG=3
    auto state = GameState::new_hand(stacks, 0, 1, 2);
    EXPECT_EQ(state.current_player(), 3);
}

TEST_F(GameStateTest, FoldAction) {
    auto state = GameState::new_hand(stacks, 0, 1, 2);
    state = state.apply_action(Action::fold()); // UTG folds
    EXPECT_EQ(state.players()[3].status, PlayerStatus::FOLDED);
    EXPECT_EQ(state.num_active_players(), 5);
}

TEST_F(GameStateTest, AllFoldToBigBlind) {
    auto state = GameState::new_hand(stacks, 0, 1, 2);

    // UTG through button fold (players 3, 4, 5, 0)
    state = state.apply_action(Action::fold()); // P3
    state = state.apply_action(Action::fold()); // P4
    state = state.apply_action(Action::fold()); // P5
    state = state.apply_action(Action::fold()); // P0 (dealer)

    // SB folds
    state = state.apply_action(Action::fold()); // P1

    // BB wins uncontested
    EXPECT_TRUE(state.is_terminal());
}

TEST_F(GameStateTest, CallAction) {
    auto state = GameState::new_hand(stacks, 0, 1, 2);
    state = state.apply_action(Action::call()); // UTG calls BB
    EXPECT_EQ(state.players()[3].stack, 198); // Called 2 chips
    EXPECT_EQ(state.players()[3].bet_this_round, 2);
}

TEST_F(GameStateTest, RaiseAction) {
    auto state = GameState::new_hand(stacks, 0, 1, 2);
    state = state.apply_action(Action::bet(6)); // UTG raises to 6
    EXPECT_EQ(state.players()[3].stack, 194);
    EXPECT_EQ(state.players()[3].bet_this_round, 6);
    EXPECT_EQ(state.current_bet(), 6);
}

TEST_F(GameStateTest, HeadsUpPreflop) {
    // Heads up: only 2 players with chips
    std::array<int32_t, MAX_PLAYERS> hu_stacks = {200, 200, 0, 0, 0, 0};
    auto state = GameState::new_hand(hu_stacks, 0, 1, 2);
    // In heads-up, dealer posts SB, other posts BB
    // Dealer (P0) acts first preflop
    EXPECT_EQ(state.current_player(), 0);
}

TEST_F(GameStateTest, LegalActionsPreflop) {
    auto state = GameState::new_hand(stacks, 0, 1, 2);
    auto actions = state.legal_actions(bet_sizes);

    // Should have fold, call, and bet options
    bool has_fold = false, has_call = false, has_bet = false;
    for (const auto& a : actions) {
        if (a.type == ActionType::FOLD) has_fold = true;
        if (a.type == ActionType::CALL) has_call = true;
        if (a.type == ActionType::BET) has_bet = true;
    }
    EXPECT_TRUE(has_fold);
    EXPECT_TRUE(has_call);
    EXPECT_TRUE(has_bet);
}

TEST_F(GameStateTest, DealFlop) {
    auto state = GameState::new_hand(stacks, 0, 1, 2);
    state = state.deal_flop(make_card(0, 0), make_card(4, 1), make_card(8, 2));
    EXPECT_EQ(state.num_board_cards(), 3);
    EXPECT_EQ(state.board()[0], make_card(0, 0));
    EXPECT_EQ(state.board()[1], make_card(4, 1));
    EXPECT_EQ(state.board()[2], make_card(8, 2));
}

TEST_F(GameStateTest, DealTurnAndRiver) {
    auto state = GameState::new_hand(stacks, 0, 1, 2);
    state = state.deal_flop(make_card(0, 0), make_card(4, 1), make_card(8, 2));
    state = state.deal_turn(make_card(12, 3));
    EXPECT_EQ(state.num_board_cards(), 4);
    state = state.deal_river(make_card(11, 0));
    EXPECT_EQ(state.num_board_cards(), 5);
}

TEST_F(GameStateTest, NonFoldedPlayersCount) {
    auto state = GameState::new_hand(stacks, 0, 1, 2);
    EXPECT_EQ(state.num_non_folded_players(), 6);

    state = state.apply_action(Action::fold()); // UTG folds
    EXPECT_EQ(state.num_non_folded_players(), 5);
}

TEST_F(GameStateTest, ShowdownPayoffs) {
    // Create a simple 2-player all-in scenario
    std::array<int32_t, MAX_PLAYERS> small_stacks = {10, 10, 0, 0, 0, 0};
    auto state = GameState::new_hand(small_stacks, 0, 1, 2);

    state.set_hole_cards(0, make_card(12, 0), make_card(12, 1)); // AA
    state.set_hole_cards(1, make_card(0, 0), make_card(1, 1));   // 23o

    // Player 0 goes all-in, player 1 calls
    state = state.apply_action(Action::bet(10));  // P0 all-in
    state = state.apply_action(Action::call());   // P1 calls

    // Deal board
    state = state.deal_flop(make_card(5, 2), make_card(7, 3), make_card(9, 0));
    state = state.deal_turn(make_card(11, 2));
    state = state.deal_river(make_card(10, 1));

    const HandEvaluator& eval = get_evaluator();
    auto payoffs = state.payoffs(eval);

    // Total should sum to 0 (zero-sum game)
    double total = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) total += payoffs[i];
    EXPECT_NEAR(total, 0.0, 0.01);
}
