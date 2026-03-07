#pragma once

#include "card.h"
#include "card_abstraction.h"
#include "action_abstraction.h"
#include "game_state.h"
#include "hand_evaluator.h"
#include "infoset_store.h"
#include "information_set.h"
#include "rng.h"
#include "deck.h"

#include <string>
#include <vector>

namespace poker {

class InteractiveTrainer {
   public:
    InteractiveTrainer(InfoSetStore& blueprint, CardAbstraction& card_abs,
                       ActionAbstraction& action_abs);

    // Main loop: play hands until the user quits
    void run();

   private:
    InfoSetStore& blueprint_;
    CardAbstraction& card_abs_;
    ActionAbstraction& action_abs_;
    const HandEvaluator& eval_;
    Rng rng_;

    int hero_seat_ = 0;
    int hands_played_ = 0;
    int total_decisions_ = 0;
    double total_deviation_ = 0.0;  // Sum of (1 - gto_freq) for each decision
    double hero_profit_ = 0.0;      // Cumulative profit in chips

    // Position names for 6-max relative to dealer
    static const char* position_name(int seat, int dealer, int num_active);

    // Display helpers
    void print_header();
    void print_hand_header(int hand_num, const GameState& state);
    void print_board(const GameState& state);
    void print_state(const GameState& state, int dealer);
    void print_action(int seat, int dealer, const Action& action, const GameState& state);
    std::string action_to_string(const Action& action, const GameState& state) const;
    std::string hand_category_str(HandRank rank) const;

    // GTO feedback: returns the GTO probability for the chosen action
    double show_gto_feedback(const GameState& state, int player, const Action& chosen_action);

    // Opponent plays according to blueprint
    Action sample_opponent_action(const GameState& state, int player);

    // Get player input
    Action get_player_action(const GameState& state);

    // Play one complete hand
    void play_hand();

    // Compute and display info set key for a state
    InfoSetKey compute_key(const GameState& state, int player) const;

    // Show session summary
    void print_session_summary();
};

}  // namespace poker
