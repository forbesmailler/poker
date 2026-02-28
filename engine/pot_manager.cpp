#include "pot_manager.h"
#include <algorithm>
#include <cassert>

namespace poker {

PotManager::PotManager() {
    round_bets_.fill(0);
    active_mask_.set(); // All players active initially
}

void PotManager::post_blind(int player, int32_t amount) {
    round_bets_[player] += amount;
}

void PotManager::add_bet(int player, int32_t amount) {
    round_bets_[player] += amount;
}

void PotManager::player_folds(int player) {
    active_mask_.reset(player);
    // Folded player's current round bets stay in the pot
    // but they are no longer eligible for future pots
    for (auto& pot : pots_) {
        pot.eligible.reset(player);
    }
}

void PotManager::finalize_round() {
    // Create side pots from round bets
    // Sort unique bet amounts, create a pot for each level

    // Collect non-zero bets
    struct PlayerBet {
        int player;
        int32_t amount;
    };
    std::vector<PlayerBet> bets;
    for (int i = 0; i < MAX_PLAYERS_CONST; ++i) {
        if (round_bets_[i] > 0) {
            bets.push_back({i, round_bets_[i]});
        }
    }

    if (bets.empty()) return;

    // Sort by bet amount
    std::sort(bets.begin(), bets.end(),
              [](const PlayerBet& a, const PlayerBet& b) {
                  return a.amount < b.amount;
              });

    int32_t prev_level = 0;
    for (size_t i = 0; i < bets.size(); ++i) {
        if (bets[i].amount <= prev_level) continue;

        int32_t level = bets[i].amount;

        SidePot pot;
        pot.amount = 0;
        pot.eligible.reset();

        // Everyone who bet at least this level contributes
        for (size_t j = 0; j < bets.size(); ++j) {
            int32_t player_contribution =
                std::min(bets[j].amount, level) -
                std::min(bets[j].amount, prev_level);
            pot.amount += player_contribution;
            if (bets[j].amount >= level && active_mask_.test(bets[j].player)) {
                pot.eligible.set(bets[j].player);
            }
        }

        // Also count folded players' contributions to this level
        // (they already contributed but aren't eligible)

        if (pot.amount > 0) {
            pots_.push_back(pot);
        }

        prev_level = level;
    }

    // Reset round bets
    round_bets_.fill(0);
}

std::array<int32_t, MAX_PLAYERS_CONST> PotManager::resolve(
    const std::array<uint16_t, MAX_PLAYERS_CONST>& hand_ranks,
    const std::bitset<MAX_PLAYERS_CONST>& active_players
) const {
    std::array<int32_t, MAX_PLAYERS_CONST> winnings;
    winnings.fill(0);

    for (const auto& pot : pots_) {
        // Find the best hand among eligible and active players
        auto eligible = pot.eligible & active_players;
        if (eligible.none()) continue;

        uint16_t best_rank = 0;
        for (int i = 0; i < MAX_PLAYERS_CONST; ++i) {
            if (eligible.test(i) && hand_ranks[i] > best_rank) {
                best_rank = hand_ranks[i];
            }
        }

        // Count winners (for split pots)
        int num_winners = 0;
        for (int i = 0; i < MAX_PLAYERS_CONST; ++i) {
            if (eligible.test(i) && hand_ranks[i] == best_rank) {
                num_winners++;
            }
        }

        // Distribute pot among winners
        int32_t share = pot.amount / num_winners;
        int32_t remainder = pot.amount % num_winners;
        int winner_idx = 0;
        for (int i = 0; i < MAX_PLAYERS_CONST; ++i) {
            if (eligible.test(i) && hand_ranks[i] == best_rank) {
                winnings[i] += share;
                // First winner gets the remainder (standard rule)
                if (winner_idx == 0) {
                    winnings[i] += remainder;
                }
                winner_idx++;
            }
        }
    }

    return winnings;
}

int32_t PotManager::total() const {
    int32_t t = 0;
    for (const auto& pot : pots_) {
        t += pot.amount;
    }
    // Also include current round bets not yet finalized
    for (int i = 0; i < MAX_PLAYERS_CONST; ++i) {
        t += round_bets_[i];
    }
    return t;
}

const std::vector<SidePot>& PotManager::pots() const {
    return pots_;
}

} // namespace poker
