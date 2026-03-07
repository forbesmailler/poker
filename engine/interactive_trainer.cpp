#include "interactive_trainer.h"
#include "generated_config.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

// Defined in win_console.cpp on Windows, always true on other platforms
bool enable_win_console_colors();

namespace poker {

static bool init_console_colors() {
    return enable_win_console_colors();
}

static bool g_colors_enabled = init_console_colors();

// ANSI color codes — empty strings if colors not supported
static const char* RESET = g_colors_enabled ? "\033[0m" : "";
static const char* BOLD = g_colors_enabled ? "\033[1m" : "";
static const char* DIM = g_colors_enabled ? "\033[2m" : "";
static const char* RED = g_colors_enabled ? "\033[31m" : "";
static const char* GREEN = g_colors_enabled ? "\033[32m" : "";
static const char* YELLOW = g_colors_enabled ? "\033[33m" : "";
static const char* BLUE = g_colors_enabled ? "\033[34m" : "";
static const char* CYAN = g_colors_enabled ? "\033[36m" : "";
static const char* WHITE = g_colors_enabled ? "\033[37m" : "";
static const char* BG_GREEN = g_colors_enabled ? "\033[42m" : "";
static const char* BG_RED = g_colors_enabled ? "\033[41m" : "";
static const char* BG_YELLOW = g_colors_enabled ? "\033[43m" : "";

// Suit display with colors
static std::string card_display(Card c) {
    if (c == CARD_NONE)
        return "??";
    uint8_t r = rank_of(c);
    uint8_t s = suit_of(c);
    std::string result;
    // Color by suit
    switch (s) {
        case 0:
            result += WHITE;
            break;  // clubs - white
        case 1:
            result += BLUE;
            break;  // diamonds - blue
        case 2:
            result += RED;
            break;  // hearts - red
        case 3:
            result += GREEN;
            break;  // spades - green
    }
    result += RANK_CHARS[r];
    // Use unicode suit symbols
    switch (s) {
        case 0:
            result += "\xe2\x99\xa3";
            break;  // clubs
        case 1:
            result += "\xe2\x99\xa6";
            break;  // diamonds
        case 2:
            result += "\xe2\x99\xa5";
            break;  // hearts
        case 3:
            result += "\xe2\x99\xa0";
            break;  // spades
    }
    result += RESET;
    return result;
}

InteractiveTrainer::InteractiveTrainer(InfoSetStore& blueprint, CardAbstraction& card_abs,
                                       ActionAbstraction& action_abs)
    : blueprint_(blueprint),
      card_abs_(card_abs),
      action_abs_(action_abs),
      eval_(get_evaluator()),
      rng_(static_cast<uint64_t>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count())) {}

const char* InteractiveTrainer::position_name(int seat, int dealer, int num_active) {
    if (num_active == 2) {
        // Heads-up: dealer=SB, other=BB
        if (seat == dealer)
            return "SB";
        return "BB";
    }
    // 6-max positions relative to dealer
    int offset = (seat - dealer + MAX_PLAYERS) % MAX_PLAYERS;
    switch (offset) {
        case 0:
            return "BTN";
        case 1:
            return "SB";
        case 2:
            return "BB";
        case 3:
            return "UTG";
        case 4:
            return "MP";
        case 5:
            return "CO";
        default:
            return "??";
    }
}

void InteractiveTrainer::print_header() {
    std::cout << "\n"
              << BOLD << CYAN << "============================================\n"
              << "       POKER GTO TRAINER (6-max NLHE)\n"
              << "============================================" << RESET << "\n"
              << DIM << "  " << config::SMALL_BLIND << "/" << config::BIG_BLIND << " blinds  |  "
              << config::STARTING_STACK << " BB starting stack\n"
              << "  Blueprint: " << blueprint_.size() << " info sets loaded" << RESET << "\n\n";
}

void InteractiveTrainer::print_board(const GameState& state) {
    int n = state.num_board_cards();
    if (n == 0) {
        std::cout << DIM << "  Board: [ - - - - - ]" << RESET << "\n";
        return;
    }
    std::cout << "  Board: [ ";
    for (int i = 0; i < n; ++i) {
        std::cout << card_display(state.board()[i]) << " ";
    }
    for (int i = n; i < 5; ++i) {
        std::cout << DIM << "- " << RESET;
    }
    std::cout << "]\n";
}

void InteractiveTrainer::print_state(const GameState& state, int dealer) {
    static const char* street_names[] = {"PREFLOP", "FLOP", "TURN", "RIVER", "SHOWDOWN"};
    int street_idx = static_cast<int>(state.street());

    std::cout << "\n" << BOLD << "--- " << street_names[street_idx] << " ---" << RESET << "\n";
    std::cout << "  Pot: " << BOLD << YELLOW << state.pot() << RESET << "\n";
    print_board(state);

    std::cout << "\n";
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        const auto& p = state.players()[i];
        if (p.status == PlayerStatus::OUT)
            continue;

        const char* pos = position_name(i, dealer, config::NUM_PLAYERS);
        bool is_hero = (i == hero_seat_);

        if (is_hero)
            std::cout << BOLD << GREEN;
        else
            std::cout << DIM;

        std::cout << "  Seat " << i << " (" << pos << "): ";

        if (p.status == PlayerStatus::FOLDED) {
            std::cout << "folded";
        } else if (p.status == PlayerStatus::ALL_IN) {
            std::cout << "ALL-IN (invested: " << p.total_invested << ")";
        } else {
            std::cout << p.stack << " chips";
            if (p.bet_this_round > 0) {
                std::cout << " (bet: " << p.bet_this_round << ")";
            }
        }

        if (is_hero) {
            std::cout << "  [" << card_display(p.hole_cards[0]) << GREEN << " "
                      << card_display(p.hole_cards[1]) << GREEN << "]";
        }

        std::cout << RESET << "\n";
    }
    std::cout << "\n";
}

std::string InteractiveTrainer::action_to_string(const Action& action,
                                                 const GameState& state) const {
    switch (action.type) {
        case ActionType::FOLD:
            return "fold";
        case ActionType::CHECK:
            return "check";
        case ActionType::CALL: {
            int to_call =
                state.current_bet() - state.players()[state.current_player()].bet_this_round;
            return "call " + std::to_string(to_call);
        }
        case ActionType::BET: {
            const auto& p = state.players()[state.current_player()];
            int all_in_amount = p.stack + p.bet_this_round;
            if (action.amount >= all_in_amount) {
                return "all-in (" + std::to_string(action.amount) + ")";
            }
            // Show as pot fraction
            int current_pot = state.pot();
            int to_call = state.current_bet() - p.bet_this_round;
            int raise_size = action.amount - p.bet_this_round - to_call;
            float pot_frac = 0.0f;
            if (current_pot + to_call > 0) {
                pot_frac =
                    static_cast<float>(raise_size) / static_cast<float>(current_pot + to_call);
            }
            std::ostringstream oss;
            oss << "bet " << action.amount;
            if (pot_frac > 0.01f) {
                oss << " (" << std::fixed << std::setprecision(0) << (pot_frac * 100) << "% pot)";
            }
            return oss.str();
        }
    }
    return "?";
}

void InteractiveTrainer::print_action(int seat, int dealer, const Action& action,
                                      const GameState& state) {
    const char* pos = position_name(seat, dealer, config::NUM_PLAYERS);
    bool is_hero = (seat == hero_seat_);

    if (is_hero)
        std::cout << GREEN << BOLD;
    else
        std::cout << DIM;

    std::cout << "  Seat " << seat << " (" << pos << ") " << action_to_string(action, state)
              << (is_hero ? " <-- you" : "") << RESET << "\n";
}

std::string InteractiveTrainer::hand_category_str(HandRank rank) const {
    int cat = HandEvaluator::category(rank);
    return HandEvaluator::category_name(cat);
}

InfoSetKey InteractiveTrainer::compute_key(const GameState& state, int player) const {
    uint16_t bucket = card_abs_.get_bucket(state.street(), state.players()[player].hole_cards[0],
                                           state.players()[player].hole_cards[1],
                                           state.board().data(), state.num_board_cards());
    return make_infoset_key(player, static_cast<int>(state.street()), bucket,
                            state.action_history_hash());
}

// Returns true if the strategy has meaningful (non-uniform) data
bool InteractiveTrainer::get_strategy(const InfoSetData* data, float* out, int num_actions) const {
    // Try average strategy first (converged GTO)
    data->average_strategy(out);

    // Check if strategy sums are all equal (uninformative uniform)
    bool avg_uniform = true;
    if (data->num_actions > 1) {
        float first = data->strategy_sum[0];
        for (int a = 1; a < data->num_actions; ++a) {
            if (std::abs(data->strategy_sum[a] - first) > 1e-3f) {
                avg_uniform = false;
                break;
            }
        }
    }

    if (!avg_uniform)
        return true;  // Average strategy is meaningful

    // Fall back to current_strategy (regret-matching)
    data->current_strategy(out);

    // Check if regrets are also all zero/uniform
    bool has_regret = false;
    for (int a = 0; a < data->num_actions; ++a) {
        if (data->cumulative_regret[a] > 1e-3f) {
            has_regret = true;
            break;
        }
    }

    return has_regret;
}

double InteractiveTrainer::show_gto_feedback(const GameState& state, int player,
                                             const Action& chosen_action) {
    auto actions = action_abs_.get_actions(state);
    int num_actions = static_cast<int>(actions.size());

    if (num_actions == 0)
        return 1.0;

    InfoSetKey key = compute_key(state, player);
    const auto* data = blueprint_.find(key);

    if (!data) {
        std::cout << DIM << "  (Info set not found in blueprint - no GTO data available)" << RESET
                  << "\n";
        return 0.5;  // Neutral when no data
    }

    float avg[InfoSetData::MAX_ACTIONS];
    bool has_data = get_strategy(data, avg, num_actions);

    if (!has_data) {
        std::cout << DIM
                  << "  (Insufficient training data for this spot - no reliable GTO strategy)"
                  << RESET << "\n";
        return 0.5;  // Neutral score
    }

    // Find which action index matches the chosen action
    // Map player's concrete action to nearest abstract action
    Action mapped = action_abs_.map_to_abstract(chosen_action, state);
    int chosen_idx = -1;
    for (int i = 0; i < num_actions; ++i) {
        if (actions[i] == mapped) {
            chosen_idx = i;
            break;
        }
    }
    // Fallback: find closest bet
    if (chosen_idx < 0 && chosen_action.type == ActionType::BET) {
        int best_diff = std::numeric_limits<int>::max();
        for (int i = 0; i < num_actions; ++i) {
            if (actions[i].type == ActionType::BET) {
                int diff = std::abs(actions[i].amount - chosen_action.amount);
                if (diff < best_diff) {
                    best_diff = diff;
                    chosen_idx = i;
                }
            }
        }
    }
    // Fallback: match by type
    if (chosen_idx < 0) {
        for (int i = 0; i < num_actions; ++i) {
            if (actions[i].type == chosen_action.type) {
                chosen_idx = i;
                break;
            }
        }
    }

    double gto_freq = (chosen_idx >= 0 && chosen_idx < num_actions) ? avg[chosen_idx] : 0.0;

    std::cout << BOLD << CYAN << "  GTO Strategy:" << RESET << "\n";
    for (int i = 0; i < num_actions; ++i) {
        float pct = avg[i] * 100.0f;
        bool is_chosen = (i == chosen_idx);

        if (is_chosen)
            std::cout << BOLD;

        // Bar chart
        int bar_len = static_cast<int>(pct / 2.5);  // 40 chars = 100%
        std::cout << "    ";
        if (is_chosen)
            std::cout << ">> ";
        else
            std::cout << "   ";

        std::cout << std::setw(20) << std::left << action_to_string(actions[i], state) << " ";

        // Color based on frequency
        if (pct >= 30.0f)
            std::cout << GREEN;
        else if (pct >= 10.0f)
            std::cout << YELLOW;
        else
            std::cout << RED;

        for (int b = 0; b < bar_len; ++b)
            std::cout << "\xe2\x96\x88";  // full block
        std::cout << " " << std::fixed << std::setprecision(1) << pct << "%" << RESET;
        if (is_chosen)
            std::cout << RESET;
        std::cout << "\n";
    }

    // Verdict
    std::cout << "\n  ";
    if (gto_freq >= 0.30) {
        std::cout << BG_GREEN << BOLD << " GOOD " << RESET << GREEN << " Strong GTO play ("
                  << std::fixed << std::setprecision(1) << (gto_freq * 100) << "% frequency)";
    } else if (gto_freq >= 0.10) {
        std::cout << BG_YELLOW << BOLD << " OK " << RESET << YELLOW
                  << " Mixed strategy - GTO plays this " << std::fixed << std::setprecision(1)
                  << (gto_freq * 100) << "% of the time";
    } else if (gto_freq >= 0.01) {
        std::cout << BG_RED << BOLD << " QUESTIONABLE " << RESET << RED << " Rare GTO line ("
                  << std::fixed << std::setprecision(1) << (gto_freq * 100) << "% frequency)";
    } else {
        std::cout << BG_RED << BOLD << " MISTAKE " << RESET << RED
                  << " GTO never/rarely takes this action";
    }
    std::cout << RESET << "\n\n";

    return gto_freq;
}

Action InteractiveTrainer::sample_opponent_action(const GameState& state, int player) {
    auto actions = action_abs_.get_actions(state);
    int num_actions = static_cast<int>(actions.size());

    if (num_actions == 0)
        return Action::check();
    if (num_actions == 1)
        return actions[0];

    InfoSetKey key = compute_key(state, player);
    const auto* data = blueprint_.find(key);

    if (!data) {
        // No blueprint data — log for debugging
        const char* pos = position_name(player, 0, config::NUM_PLAYERS);
        std::cout << DIM << "  [debug] No blueprint for seat " << player << " bucket="
                  << card_abs_.get_bucket(state.street(), state.players()[player].hole_cards[0],
                                          state.players()[player].hole_cards[1],
                                          state.board().data(), state.num_board_cards())
                  << " hash=" << state.action_history_hash() << RESET << "\n";
        // Default to check/fold
        for (const auto& a : actions)
            if (a.type == ActionType::CHECK)
                return a;
        return actions[0];
    }

    float strat[InfoSetData::MAX_ACTIONS];
    bool has_data = get_strategy(data, strat, num_actions);
    if (!has_data) {
        // Insufficient data: check/fold
        for (const auto& a : actions)
            if (a.type == ActionType::CHECK)
                return a;
        return actions[0];
    }
    int idx = rng_.sample_action(strat, std::min(num_actions, static_cast<int>(data->num_actions)));
    if (idx >= num_actions)
        idx = num_actions - 1;
    return actions[idx];
}

Action InteractiveTrainer::get_player_action(const GameState& state) {
    auto actions = action_abs_.get_actions(state);
    int num_actions = static_cast<int>(actions.size());

    std::cout << BOLD << "  Your turn! Choose an action:" << RESET << "\n";
    for (int i = 0; i < num_actions; ++i) {
        std::cout << "    " << BOLD << (i + 1) << RESET << ") "
                  << action_to_string(actions[i], state) << "\n";
    }

    while (true) {
        std::cout << "\n  " << GREEN << "> " << RESET;
        std::string input;
        if (!std::getline(std::cin, input)) {
            return Action::fold();  // EOF
        }

        // Trim whitespace
        size_t start = input.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            continue;
        input = input.substr(start);

        // Check for quit
        if (input == "q" || input == "quit" || input == "exit") {
            return Action{ActionType::FOLD, -1};  // Sentinel for quit
        }

        // Accept number
        try {
            int choice = std::stoi(input);
            if (choice >= 1 && choice <= num_actions) {
                return actions[choice - 1];
            }
        } catch (...) {
        }

        // Accept action names
        std::string lower = input;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower == "f" || lower == "fold") {
            for (auto& a : actions)
                if (a.type == ActionType::FOLD)
                    return a;
        }
        if (lower == "x" || lower == "check") {
            for (auto& a : actions)
                if (a.type == ActionType::CHECK)
                    return a;
        }
        if (lower == "c" || lower == "call") {
            for (auto& a : actions)
                if (a.type == ActionType::CALL)
                    return a;
        }
        if (lower == "a" || lower == "allin" || lower == "all-in" || lower == "all in") {
            // Find the largest bet (all-in)
            for (int i = num_actions - 1; i >= 0; --i) {
                if (actions[i].type == ActionType::BET)
                    return actions[i];
            }
        }

        std::cout << RED << "  Invalid choice. Enter 1-" << num_actions
                  << ", or f/x/c/a, or 'q' to quit." << RESET << "\n";
    }
}

void InteractiveTrainer::play_hand() {
    hands_played_++;
    int dealer = rng_.next_int(MAX_PLAYERS);

    // Initialize stacks
    std::array<int32_t, MAX_PLAYERS> stacks;
    stacks.fill(config::STARTING_STACK);

    GameState state = GameState::new_hand(stacks, dealer, config::SMALL_BLIND, config::BIG_BLIND);

    // Deal hole cards to all players
    Deck deck;
    deck.shuffle(rng_);

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Card c0 = deck.deal();
        Card c1 = deck.deal();
        state.set_hole_cards(i, c0, c1);
    }

    std::cout << "\n"
              << BOLD << CYAN << "========== Hand #" << hands_played_ << " ==========" << RESET
              << "\n";
    const char* hero_pos = position_name(hero_seat_, dealer, config::NUM_PLAYERS);
    std::cout << "  You are Seat " << hero_seat_ << " (" << BOLD << hero_pos << RESET << ")\n";
    std::cout << "  Your hand: " << card_display(state.players()[hero_seat_].hole_cards[0]) << " "
              << card_display(state.players()[hero_seat_].hole_cards[1]) << "\n";

    Street last_printed_street = Street::SHOWDOWN;  // Force initial print
    bool quit = false;

    // Main hand loop
    while (!state.is_terminal()) {
        if (state.is_chance_node()) {
            // Deal community cards
            if (state.street() == Street::FLOP && state.num_board_cards() < 3) {
                Card c0 = deck.deal(), c1 = deck.deal(), c2 = deck.deal();
                state = state.deal_flop(c0, c1, c2);
            } else if (state.street() == Street::TURN && state.num_board_cards() < 4) {
                state = state.deal_turn(deck.deal());
            } else if (state.street() == Street::RIVER && state.num_board_cards() < 5) {
                state = state.deal_river(deck.deal());
            } else {
                // All-in runout
                state.advance_to_showdown();
            }
            continue;
        }

        // Print state when street changes
        if (state.street() != last_printed_street) {
            print_state(state, dealer);
            last_printed_street = state.street();
        }

        int player = state.current_player();

        if (player == hero_seat_) {
            // Player's turn
            Action action = get_player_action(state);

            // Check for quit sentinel
            if (action.amount == -1 && action.type == ActionType::FOLD) {
                quit = true;
                break;
            }

            print_action(player, dealer, action, state);

            // Show GTO feedback
            double gto_freq = show_gto_feedback(state, player, action);
            total_decisions_++;
            total_deviation_ += (1.0 - gto_freq);

            state = state.apply_action(action);
        } else {
            // Opponent's turn
            Action action = sample_opponent_action(state, player);
            print_action(player, dealer, action, state);
            state = state.apply_action(action);
        }
    }

    if (quit)
        return;

    // Show results
    std::cout << BOLD << "\n--- RESULTS ---" << RESET << "\n";

    if (state.num_non_folded_players() <= 1) {
        // Someone won by everyone folding
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (state.players()[i].status != PlayerStatus::FOLDED &&
                state.players()[i].status != PlayerStatus::OUT) {
                const char* pos = position_name(i, dealer, config::NUM_PLAYERS);
                if (i == hero_seat_)
                    std::cout << GREEN << BOLD;
                std::cout << "  Seat " << i << " (" << pos << ") wins " << state.pot()
                          << " (everyone folded)";
                if (i == hero_seat_)
                    std::cout << RESET;
                std::cout << "\n";
                break;
            }
        }
    } else {
        // Showdown - show all remaining hands
        print_board(state);
        std::cout << "\n";

        for (int i = 0; i < MAX_PLAYERS; ++i) {
            const auto& p = state.players()[i];
            if (p.status == PlayerStatus::FOLDED || p.status == PlayerStatus::OUT)
                continue;

            const char* pos = position_name(i, dealer, config::NUM_PLAYERS);
            HandRank rank =
                eval_.evaluate(p.hole_cards[0], p.hole_cards[1], state.board()[0], state.board()[1],
                               state.board()[2], state.board()[3], state.board()[4]);
            std::cout << "  Seat " << i << " (" << pos << "): " << card_display(p.hole_cards[0])
                      << " " << card_display(p.hole_cards[1]) << "  " << DIM << "("
                      << hand_category_str(rank) << ")" << RESET << "\n";
        }
    }

    // Compute payoffs
    auto payoffs = state.payoffs(eval_);
    double hero_payoff = payoffs[hero_seat_];
    hero_profit_ += hero_payoff;

    std::cout << "\n  ";
    if (hero_payoff > 0) {
        std::cout << GREEN << BOLD << "You won +" << hero_payoff << " chips!";
    } else if (hero_payoff < 0) {
        std::cout << RED << BOLD << "You lost " << hero_payoff << " chips.";
    } else {
        std::cout << YELLOW << "Break even (0 chips).";
    }
    std::cout << RESET << "\n";

    // Session running stats
    std::cout << DIM << "  Session: " << hands_played_ << " hands, "
              << (hero_profit_ >= 0 ? "+" : "") << hero_profit_ << " chips";
    if (total_decisions_ > 0) {
        double avg_gto = 1.0 - (total_deviation_ / total_decisions_);
        std::cout << ", GTO accuracy: " << std::fixed << std::setprecision(1) << (avg_gto * 100)
                  << "%";
    }
    std::cout << RESET << "\n";
}

void InteractiveTrainer::print_session_summary() {
    std::cout << "\n"
              << BOLD << CYAN << "============================================\n"
              << "             SESSION SUMMARY\n"
              << "============================================" << RESET << "\n";

    std::cout << "  Hands played:   " << hands_played_ << "\n";
    std::cout << "  Total profit:   ";
    if (hero_profit_ >= 0)
        std::cout << GREEN;
    else
        std::cout << RED;
    std::cout << BOLD << (hero_profit_ >= 0 ? "+" : "") << hero_profit_ << " chips" << RESET
              << "\n";

    if (hands_played_ > 0) {
        double bb_per_hand = hero_profit_ / hands_played_ / config::BIG_BLIND;
        std::cout << "  BB/hand:        ";
        if (bb_per_hand >= 0)
            std::cout << GREEN;
        else
            std::cout << RED;
        std::cout << std::fixed << std::setprecision(2) << bb_per_hand << RESET << "\n";
    }

    if (total_decisions_ > 0) {
        double avg_gto = 1.0 - (total_deviation_ / total_decisions_);
        std::cout << "  Decisions made: " << total_decisions_ << "\n";
        std::cout << "  GTO accuracy:   ";
        if (avg_gto >= 0.70)
            std::cout << GREEN;
        else if (avg_gto >= 0.40)
            std::cout << YELLOW;
        else
            std::cout << RED;
        std::cout << BOLD << std::fixed << std::setprecision(1) << (avg_gto * 100) << "%" << RESET
                  << "\n";

        if (avg_gto >= 0.80)
            std::cout << GREEN << "  Excellent! You're playing very close to GTO." << RESET << "\n";
        else if (avg_gto >= 0.60)
            std::cout << YELLOW << "  Solid play with room for improvement." << RESET << "\n";
        else if (avg_gto >= 0.40)
            std::cout << YELLOW << "  You have significant leaks to work on." << RESET << "\n";
        else
            std::cout << RED << "  Study GTO concepts - lots of room for growth!" << RESET << "\n";
    }
    std::cout << "\n";
}

void InteractiveTrainer::run() {
    // Force unbuffered output so prompts appear immediately
    std::cout << std::unitbuf;

    print_header();

    // Seat selection
    std::cout << "Choose your seat (0-" << (MAX_PLAYERS - 1) << "), or press Enter for random: ";
    std::string input;
    if (std::getline(std::cin, input)) {
        size_t start = input.find_first_not_of(" \t\r\n");
        if (start != std::string::npos) {
            try {
                int seat = std::stoi(input.substr(start));
                if (seat >= 0 && seat < MAX_PLAYERS) {
                    hero_seat_ = seat;
                }
            } catch (...) {
            }
        } else {
            hero_seat_ = rng_.next_int(MAX_PLAYERS);
        }
    }

    std::cout << "\n" << BOLD << "You are seated at Seat " << hero_seat_ << RESET << "\n";
    std::cout << DIM << "  Type a number to pick an action, or:\n"
              << "    f=fold  x=check  c=call  a=all-in  q=quit\n"
              << "  Press Enter to start..." << RESET << "\n";
    std::getline(std::cin, input);

    while (true) {
        play_hand();

        std::cout << "\n" << DIM << "Press Enter for next hand, or 'q' to quit: " << RESET;
        if (!std::getline(std::cin, input))
            break;
        size_t start = input.find_first_not_of(" \t\r\n");
        if (start != std::string::npos) {
            std::string trimmed = input.substr(start);
            if (trimmed == "q" || trimmed == "quit" || trimmed == "exit")
                break;
        }
    }

    print_session_summary();
}

}  // namespace poker
