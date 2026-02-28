#include "deck.h"
#include <algorithm>

namespace poker {

Deck::Deck() {
    reset();
}

void Deck::reset() {
    for (int i = 0; i < 52; ++i) {
        cards_[i] = static_cast<Card>(i);
    }
    top_ = 0;
    dealt_ = 0;
}

void Deck::shuffle(Rng& rng) {
    // Only shuffle remaining cards (from top_ onward)
    int n = 52 - top_;
    rng.shuffle(cards_.data() + top_, n);
}

Card Deck::deal() {
    while (top_ < 52) {
        Card c = cards_[top_++];
        if (!(dealt_ & card_bit(c))) {
            dealt_ |= card_bit(c);
            return c;
        }
    }
    return CARD_NONE;
}

void Deck::remove(Card c) {
    dealt_ |= card_bit(c);
}

int Deck::remaining() const {
    int count = 0;
    for (int i = top_; i < 52; ++i) {
        if (!(dealt_ & card_bit(cards_[i]))) {
            ++count;
        }
    }
    return count;
}

} // namespace poker
