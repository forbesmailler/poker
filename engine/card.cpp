#include "card.h"
#include <stdexcept>

namespace poker {

std::string card_to_string(Card c) {
    if (c >= NUM_CARDS)
        return "??";
    std::string s;
    s += RANK_CHARS[rank_of(c)];
    s += SUIT_CHARS[suit_of(c)];
    return s;
}

Card string_to_card(const std::string& s) {
    if (s.size() != 2) {
        throw std::invalid_argument("Card string must be 2 characters: " + s);
    }

    int rank = -1;
    for (int i = 0; i < NUM_RANKS; ++i) {
        if (RANK_CHARS[i] == s[0]) {
            rank = i;
            break;
        }
    }
    if (rank < 0) {
        throw std::invalid_argument("Invalid rank character: " + s);
    }

    int suit = -1;
    for (int i = 0; i < NUM_SUITS; ++i) {
        if (SUIT_CHARS[i] == s[1]) {
            suit = i;
            break;
        }
    }
    if (suit < 0) {
        throw std::invalid_argument("Invalid suit character: " + s);
    }

    return make_card(static_cast<uint8_t>(rank), static_cast<uint8_t>(suit));
}

}  // namespace poker
