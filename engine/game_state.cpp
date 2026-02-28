#include "game_state.h"
#include "generated_config.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace poker {

GameState GameState::new_hand(const std::array<int32_t, MAX_PLAYERS>& stacks, int dealer_pos,
                              int small_blind, int big_blind) {
    GameState state;
    state.dealer_pos_ = dealer_pos;
    state.small_blind_ = small_blind;
    state.big_blind_ = big_blind;
    state.street_ = Street::PREFLOP;
    state.board_.fill(CARD_NONE);
    state.num_board_cards_ = 0;
    state.action_hash_ = 0;
    state.last_raiser_ = -1;
    state.num_actions_this_round_ = 0;
    state.num_raises_this_round_ = 0;
    state.current_bet_ = big_blind;
    state.num_players_acted_ = 0;
    state.first_action_ = true;

    // Initialize players
    int num_active = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        state.players_[i].stack = stacks[i];
        state.players_[i].bet_this_round = 0;
        state.players_[i].total_invested = 0;
        state.players_[i].hole_cards = {CARD_NONE, CARD_NONE};

        if (stacks[i] > 0) {
            state.players_[i].status = PlayerStatus::ACTIVE;
            num_active++;
        } else {
            state.players_[i].status = PlayerStatus::OUT;
        }
    }

    // Post blinds
    // In 6-max: dealer_pos, SB = dealer+1, BB = dealer+2
    // Heads-up: dealer posts SB, other posts BB
    int sb_pos, bb_pos;
    if (num_active == 2) {
        sb_pos = dealer_pos;
        bb_pos = state.next_active_player(dealer_pos);
    } else {
        sb_pos = state.next_active_player(dealer_pos);
        bb_pos = state.next_active_player(sb_pos);
    }

    // Post small blind
    int sb_amount = std::min(small_blind, state.players_[sb_pos].stack);
    state.players_[sb_pos].stack -= sb_amount;
    state.players_[sb_pos].bet_this_round = sb_amount;
    state.players_[sb_pos].total_invested = sb_amount;
    state.pot_manager_.post_blind(sb_pos, sb_amount);
    if (state.players_[sb_pos].stack == 0) {
        state.players_[sb_pos].status = PlayerStatus::ALL_IN;
    }

    // Post big blind
    int bb_amount = std::min(big_blind, state.players_[bb_pos].stack);
    state.players_[bb_pos].stack -= bb_amount;
    state.players_[bb_pos].bet_this_round = bb_amount;
    state.players_[bb_pos].total_invested = bb_amount;
    state.pot_manager_.post_blind(bb_pos, bb_amount);
    if (state.players_[bb_pos].stack == 0) {
        state.players_[bb_pos].status = PlayerStatus::ALL_IN;
    }

    // First to act preflop: player after BB (UTG)
    state.current_player_ = state.next_active_player(bb_pos);
    // In heads-up, SB acts first preflop
    if (num_active == 2) {
        state.current_player_ = sb_pos;
    }

    return state;
}

void GameState::set_hole_cards(int player, Card c0, Card c1) {
    players_[player].hole_cards[0] = c0;
    players_[player].hole_cards[1] = c1;
}

int GameState::next_active_player(int from) const {
    for (int i = 1; i <= MAX_PLAYERS; ++i) {
        int idx = (from + i) % MAX_PLAYERS;
        if (players_[idx].status == PlayerStatus::ACTIVE) {
            return idx;
        }
    }
    return -1;
}

int GameState::num_active_players() const {
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (players_[i].status == PlayerStatus::ACTIVE) {
            count++;
        }
    }
    return count;
}

int GameState::num_non_folded_players() const {
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (players_[i].status == PlayerStatus::ACTIVE ||
            players_[i].status == PlayerStatus::ALL_IN) {
            count++;
        }
    }
    return count;
}

bool GameState::is_terminal() const {
    if (street_ == Street::SHOWDOWN)
        return true;

    // Only one non-folded player remaining
    if (num_non_folded_players() <= 1)
        return true;

    return false;
}

bool GameState::is_chance_node() const {
    if (is_terminal())
        return false;

    // A chance node is any state where board cards need to be dealt
    // before players can act on the current street.
    if (street_ == Street::FLOP && num_board_cards_ < 3)
        return true;
    if (street_ == Street::TURN && num_board_cards_ < 4)
        return true;
    if (street_ == Street::RIVER && num_board_cards_ < 5)
        return true;

    // All-in runout: no one can act but we haven't reached showdown.
    // Cards for the current street are dealt, but we need to advance
    // to deal more streets. Treat as chance node so MCCFR advances.
    if (num_active_players() == 0 && num_non_folded_players() > 1 && street_ != Street::SHOWDOWN) {
        return true;
    }

    return false;
}

int GameState::current_player() const {
    return current_player_;
}

Street GameState::street() const {
    return street_;
}

std::vector<Action> GameState::legal_actions(const std::vector<BetSize>& allowed_bets) const {
    std::vector<Action> actions;

    if (is_terminal() || is_chance_node())
        return actions;

    const auto& player = players_[current_player_];
    if (player.status != PlayerStatus::ACTIVE)
        return actions;

    int to_call = current_bet_ - player.bet_this_round;

    if (to_call > 0) {
        // Can fold
        actions.push_back(Action::fold());

        // Can call
        actions.push_back(Action::call());
    } else {
        // Can check
        actions.push_back(Action::check());
    }

    // Can raise/bet if not too many raises already
    if (num_raises_this_round_ < config::MAX_RAISES_PER_ROUND) {
        int current_pot = pot();
        int min_raise = big_blind_;
        if (current_bet_ > 0) {
            min_raise = current_bet_;  // Minimum raise is the size of the last raise
        }

        for (const auto& bet_size : allowed_bets) {
            int amount;
            if (bet_size.all_in) {
                amount = player.stack + player.bet_this_round;
            } else {
                amount =
                    player.bet_this_round + to_call +
                    static_cast<int>(std::round(bet_size.pot_fraction * (current_pot + to_call)));
            }

            // Clamp to valid range
            int min_total_bet = current_bet_ + min_raise;
            int max_total_bet = player.stack + player.bet_this_round;

            if (amount < min_total_bet)
                amount = min_total_bet;
            if (amount > max_total_bet)
                amount = max_total_bet;

            if (amount <= current_bet_)
                continue;  // Not a valid raise
            if (amount > max_total_bet)
                continue;  // Can't afford

            // Avoid duplicate bet sizes
            bool duplicate = false;
            for (const auto& a : actions) {
                if (a.type == ActionType::BET && a.amount == amount) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                actions.push_back(Action::bet(amount));
            }
        }

        // Always include all-in if configured and not already present
        if (config::INCLUDE_ALL_IN) {
            int all_in_amount = player.stack + player.bet_this_round;
            if (all_in_amount > current_bet_) {
                bool has_all_in = false;
                for (const auto& a : actions) {
                    if (a.type == ActionType::BET && a.amount == all_in_amount) {
                        has_all_in = true;
                        break;
                    }
                }
                if (!has_all_in) {
                    actions.push_back(Action::bet(all_in_amount));
                }
            }
        }

        // Sort bet actions by amount
        std::sort(actions.begin(), actions.end(), [](const Action& a, const Action& b) {
            if (a.type != b.type) {
                return static_cast<int>(a.type) < static_cast<int>(b.type);
            }
            return a.amount < b.amount;
        });
    }

    return actions;
}

GameState GameState::apply_action(const Action& action) const {
    GameState next = *this;
    auto& player = next.players_[next.current_player_];

    // Update action hash
    next.action_hash_ ^= static_cast<uint64_t>(action.type) << (next.num_actions_this_round_ * 4);
    next.action_hash_ = next.action_hash_ * 2654435761ULL + static_cast<uint64_t>(action.amount);

    switch (action.type) {
        case ActionType::FOLD:
            player.status = PlayerStatus::FOLDED;
            next.pot_manager_.player_folds(next.current_player_);
            break;

        case ActionType::CHECK:
            break;

        case ActionType::CALL: {
            int to_call = std::min(next.current_bet_ - player.bet_this_round, player.stack);
            player.stack -= to_call;
            player.bet_this_round += to_call;
            player.total_invested += to_call;
            next.pot_manager_.add_bet(next.current_player_, to_call);
            if (player.stack == 0) {
                player.status = PlayerStatus::ALL_IN;
            }
            break;
        }

        case ActionType::BET: {
            int total_bet = action.amount;
            int additional = total_bet - player.bet_this_round;
            additional = std::min(additional, player.stack);
            player.stack -= additional;
            player.bet_this_round += additional;
            player.total_invested += additional;
            next.pot_manager_.add_bet(next.current_player_, additional);
            next.current_bet_ = player.bet_this_round;
            next.last_raiser_ = next.current_player_;
            next.num_raises_this_round_++;
            if (player.stack == 0) {
                player.status = PlayerStatus::ALL_IN;
            }
            // Reset: everyone needs to act again after a raise
            next.num_players_acted_ = 0;
            break;
        }
    }

    next.num_actions_this_round_++;
    next.num_players_acted_++;
    next.first_action_ = false;

    // Check if we should move to the next street or end the hand
    if (next.num_non_folded_players() <= 1) {
        // Everyone folded except one player — hand is over
        next.street_ = Street::SHOWDOWN;
        next.pot_manager_.finalize_round();
        return next;
    }

    // Advance to next active player
    next.advance_to_next_player();

    // Check if betting round is complete
    if (next.is_round_complete()) {
        next.pot_manager_.finalize_round();

        if (next.num_active_players() <= 1) {
            // All but one (or zero) are all-in or folded
            // Deal remaining cards and go to showdown
            next.advance_street();
        } else {
            next.advance_street();
        }
    }

    return next;
}

bool GameState::is_round_complete() const {
    // Round is complete when every active player has acted at least once
    // and all active players have matched the current bet (or are all-in)

    if (first_action_)
        return false;

    // If current player is the last raiser, everyone else has responded
    if (num_active_players() == 0)
        return true;

    // Check that all active players have equal bets
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (players_[i].status == PlayerStatus::ACTIVE) {
            if (players_[i].bet_this_round < current_bet_) {
                return false;
            }
        }
    }

    // All active players have matched — but have they all acted?
    if (last_raiser_ >= 0 && current_player_ == last_raiser_) {
        return true;
    }

    // If no raises, everyone must have acted
    // Check: has the action gone around?
    return num_players_acted_ >= num_active_players();
}

void GameState::advance_to_next_player() {
    int start = current_player_;
    for (int i = 1; i <= MAX_PLAYERS; ++i) {
        int idx = (start + i) % MAX_PLAYERS;
        if (players_[idx].status == PlayerStatus::ACTIVE) {
            current_player_ = idx;
            return;
        }
    }
    // No active players found
    current_player_ = -1;
}

int GameState::first_to_act_postflop() const {
    // First active player after dealer
    return next_active_player(dealer_pos_);
}

int GameState::first_to_act_preflop() const {
    // UTG: first player after BB
    // Find BB position
    int sb_pos = next_active_player(dealer_pos_);
    int bb_pos = next_active_player(sb_pos);
    return next_active_player(bb_pos);
}

void GameState::advance_to_showdown() {
    // Advance to the next street during an all-in runout.
    // Called when cards for the current street are dealt but no one can act.
    advance_street();
}

void GameState::advance_street() {
    switch (street_) {
        case Street::PREFLOP:
            street_ = Street::FLOP;
            break;
        case Street::FLOP:
            street_ = Street::TURN;
            break;
        case Street::TURN:
            street_ = Street::RIVER;
            break;
        case Street::RIVER:
            street_ = Street::SHOWDOWN;
            return;
        case Street::SHOWDOWN:
            return;
    }

    // If everyone is all-in, go straight to showdown
    if (num_active_players() <= 1 && street_ != Street::SHOWDOWN) {
        // Will need to deal remaining cards, but action is done
        // The caller (MCCFR) will handle dealing
    }

    // Reset for new street
    current_bet_ = 0;
    last_raiser_ = -1;
    num_actions_this_round_ = 0;
    num_raises_this_round_ = 0;
    num_players_acted_ = 0;
    first_action_ = true;

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        players_[i].bet_this_round = 0;
    }

    // Set first to act (after dealer, postflop)
    int first = first_to_act_postflop();
    if (first >= 0) {
        current_player_ = first;
    }
}

GameState GameState::deal_flop(Card c0, Card c1, Card c2) const {
    GameState next = *this;
    next.board_[0] = c0;
    next.board_[1] = c1;
    next.board_[2] = c2;
    next.num_board_cards_ = 3;
    return next;
}

GameState GameState::deal_turn(Card c) const {
    GameState next = *this;
    next.board_[3] = c;
    next.num_board_cards_ = 4;
    return next;
}

GameState GameState::deal_river(Card c) const {
    GameState next = *this;
    next.board_[4] = c;
    next.num_board_cards_ = 5;
    return next;
}

std::array<double, MAX_PLAYERS> GameState::payoffs(const HandEvaluator& eval) const {
    std::array<double, MAX_PLAYERS> result;
    result.fill(0.0);

    // If only one player left (everyone else folded)
    int non_folded = num_non_folded_players();
    if (non_folded <= 1) {
        int winner = -1;
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (players_[i].status != PlayerStatus::FOLDED &&
                players_[i].status != PlayerStatus::OUT) {
                winner = i;
                break;
            }
        }
        if (winner >= 0) {
            int total_pot = pot_manager_.total();
            for (int i = 0; i < MAX_PLAYERS; ++i) {
                result[i] = -static_cast<double>(players_[i].total_invested);
            }
            result[winner] += total_pot;
        }
        return result;
    }

    // Showdown — evaluate hands
    std::array<uint16_t, MAX_PLAYERS> hand_ranks;
    hand_ranks.fill(0);
    std::bitset<MAX_PLAYERS_CONST> active;

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (players_[i].status == PlayerStatus::FOLDED || players_[i].status == PlayerStatus::OUT) {
            continue;
        }
        active.set(i);

        if (num_board_cards_ >= 5) {
            hand_ranks[i] = eval.evaluate(players_[i].hole_cards[0], players_[i].hole_cards[1],
                                          board_[0], board_[1], board_[2], board_[3], board_[4]);
        }
    }

    auto winnings = pot_manager_.resolve(hand_ranks, active);

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        result[i] =
            static_cast<double>(winnings[i]) - static_cast<double>(players_[i].total_invested);
    }

    return result;
}

}  // namespace poker
