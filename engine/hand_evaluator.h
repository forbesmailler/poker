#pragma once

#include "card.h"
#include <cstdint>
#include <array>
#include <vector>

namespace poker {

// Hand rank: uint16_t where higher = stronger
// category = rank >> 12 (0=high card, 1=pair, ..., 8=straight flush)
// sub-rank = rank & 0xFFF (ordering within category)
using HandRank = uint16_t;

// Hand categories
enum HandCategory : int {
    HIGH_CARD = 0,
    ONE_PAIR = 1,
    TWO_PAIR = 2,
    THREE_OF_A_KIND = 3,
    STRAIGHT = 4,
    FLUSH = 5,
    FULL_HOUSE = 6,
    FOUR_OF_A_KIND = 7,
    STRAIGHT_FLUSH = 8,
};

class HandEvaluator {
public:
    HandEvaluator();

    // Evaluate best 5-card hand from 7 cards
    HandRank evaluate(Card c0, Card c1, Card c2,
                      Card c3, Card c4, Card c5, Card c6) const;

    // Evaluate from array (n = 5, 6, or 7)
    HandRank evaluate(const Card* cards, int n) const;

    static int category(HandRank r) { return r >> 12; }
    static int compare(HandRank a, HandRank b) {
        return static_cast<int>(a) - static_cast<int>(b);
    }

    static const char* category_name(int cat);

private:
    // Flush lookup: indexed by bit pattern of ranks in flush suit (8192 entries)
    std::array<HandRank, 8192> flush_table_;

    // Non-flush: indexed by rank hash (unique-5 table)
    std::array<HandRank, 8192> unique5_table_;

    // Products-of-primes approach for general hands
    // We use a hash table for the remaining non-flush, non-unique hands
    static constexpr int HASH_TABLE_SIZE = 32769;
    std::array<HandRank, HASH_TABLE_SIZE> hash_table_;
    std::array<uint32_t, HASH_TABLE_SIZE> hash_keys_;

    static constexpr std::array<int, 13> RANK_PRIMES = {
        2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41
    };

    void init_tables();
    void init_flush_table();
    void init_unique5_table();
    void init_remaining_table();

    HandRank eval5(Card c0, Card c1, Card c2, Card c3, Card c4) const;
    HandRank lookup_flush(uint16_t rank_bits) const;
    HandRank lookup_unique5(uint16_t rank_bits) const;
    uint32_t hash_find(uint32_t key) const;
};

// Thread-safe singleton
const HandEvaluator& get_evaluator();

} // namespace poker
