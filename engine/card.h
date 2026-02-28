#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace poker {

// Card encoded as uint8_t: 0-51
// rank = card / 4  (0=2, 1=3, ..., 12=A)
// suit = card % 4  (0=clubs, 1=diamonds, 2=hearts, 3=spades)
using Card = uint8_t;

constexpr uint8_t NUM_CARDS = 52;
constexpr uint8_t NUM_RANKS = 13;
constexpr uint8_t NUM_SUITS = 4;
constexpr Card CARD_NONE = 255;

constexpr uint8_t rank_of(Card c) {
    return c / 4;
}
constexpr uint8_t suit_of(Card c) {
    return c % 4;
}
constexpr Card make_card(uint8_t rank, uint8_t suit) {
    return static_cast<Card>(rank * 4 + suit);
}

std::string card_to_string(Card c);
Card string_to_card(const std::string& s);

// 64-bit bitmask for card sets (bits 0-51)
using CardMask = uint64_t;
constexpr CardMask card_bit(Card c) {
    return 1ULL << c;
}

constexpr std::array<char, 13> RANK_CHARS = {'2', '3', '4', '5', '6', '7', '8',
                                             '9', 'T', 'J', 'Q', 'K', 'A'};
constexpr std::array<char, 4> SUIT_CHARS = {'c', 'd', 'h', 's'};

}  // namespace poker
