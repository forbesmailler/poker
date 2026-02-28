#pragma once

#include <vector>
#include <cstdint>
#include <bitset>
#include <array>
#include <cstring>

namespace poker {

constexpr int MAX_PLAYERS_CONST = 6;

struct SidePot {
    int32_t amount;
    std::bitset<MAX_PLAYERS_CONST> eligible;
};

class PotManager {
   public:
    PotManager();

    void post_blind(int player, int32_t amount);
    void add_bet(int player, int32_t amount);
    void player_folds(int player);
    void finalize_round();

    // Resolve pots given hand ranks and active players
    // Returns net chip change per player (won - invested)
    std::array<int32_t, MAX_PLAYERS_CONST> resolve(
        const std::array<uint16_t, MAX_PLAYERS_CONST>& hand_ranks,
        const std::bitset<MAX_PLAYERS_CONST>& active_players) const;

    int32_t total() const;
    const std::vector<SidePot>& pots() const;

    const std::array<int32_t, MAX_PLAYERS_CONST>& round_bets() const { return round_bets_; }

    int32_t player_round_bet(int player) const { return round_bets_[player]; }

   private:
    std::vector<SidePot> pots_;
    std::array<int32_t, MAX_PLAYERS_CONST> round_bets_;
    std::bitset<MAX_PLAYERS_CONST> active_mask_;
};

}  // namespace poker
