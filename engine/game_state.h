#pragma once

#include "action.h"
#include "card.h"
#include "hand_evaluator.h"
#include "pot_manager.h"
#include <array>
#include <vector>
#include <cstdint>

namespace poker {

constexpr int MAX_PLAYERS = 6;

enum class Street : uint8_t {
    PREFLOP = 0,
    FLOP = 1,
    TURN = 2,
    RIVER = 3,
    SHOWDOWN = 4,
};

enum class PlayerStatus : uint8_t {
    ACTIVE,
    FOLDED,
    ALL_IN,
    OUT,  // Not in this hand (e.g., sitting out)
};

struct PlayerState {
    int32_t stack;
    int32_t bet_this_round;
    int32_t total_invested;
    PlayerStatus status;
    std::array<Card, 2> hole_cards;
};

class GameState {
   public:
    static GameState new_hand(const std::array<int32_t, MAX_PLAYERS>& stacks, int dealer_pos,
                              int small_blind, int big_blind);

    bool is_terminal() const;
    bool is_chance_node() const;
    int current_player() const;
    Street street() const;

    std::vector<Action> legal_actions(const std::vector<BetSize>& allowed_bets) const;

    // State transitions
    GameState apply_action(const Action& action) const;
    GameState deal_flop(Card c0, Card c1, Card c2) const;
    GameState deal_turn(Card c) const;
    GameState deal_river(Card c) const;

    // Set hole cards for a player
    void set_hole_cards(int player, Card c0, Card c1);

    // Payoff at terminal (chip amounts won/lost)
    std::array<double, MAX_PLAYERS> payoffs(const HandEvaluator& eval) const;

    const std::array<PlayerState, MAX_PLAYERS>& players() const { return players_; }
    const std::array<Card, 5>& board() const { return board_; }
    int num_board_cards() const { return num_board_cards_; }
    int pot() const { return pot_manager_.total(); }
    int num_active_players() const;
    int num_non_folded_players() const;

    // Advance street during all-in runout (no players can act)
    void advance_to_showdown();

    // Hash of action history for info set key
    uint64_t action_history_hash() const { return action_hash_; }

    // Number of raises in current betting round
    int num_raises_this_round() const { return num_raises_this_round_; }

    int current_bet() const { return current_bet_; }
    int big_blind() const { return big_blind_; }
    int small_blind() const { return small_blind_; }

   private:
    std::array<PlayerState, MAX_PLAYERS> players_;
    std::array<Card, 5> board_;
    int num_board_cards_ = 0;
    Street street_ = Street::PREFLOP;
    int dealer_pos_ = 0;
    int current_player_ = 0;
    int last_raiser_ = -1;
    int num_actions_this_round_ = 0;
    int num_raises_this_round_ = 0;
    int current_bet_ = 0;
    int small_blind_ = 1;
    int big_blind_ = 2;
    int num_players_acted_ = 0;
    bool first_action_ = true;
    PotManager pot_manager_;
    uint64_t action_hash_ = 0;

    void advance_to_next_player();
    void advance_street();
    bool is_round_complete() const;
    int next_active_player(int from) const;
    int first_to_act_postflop() const;
    int first_to_act_preflop() const;
};

}  // namespace poker
