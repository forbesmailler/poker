#pragma once

#include "card.h"
#include "rng.h"
#include <array>

namespace poker {

class Deck {
public:
    Deck();

    void shuffle(Rng& rng);
    Card deal();
    void remove(Card c);
    void reset();
    int remaining() const;

private:
    std::array<Card, 52> cards_;
    int top_ = 0;
    CardMask dealt_ = 0;
};

} // namespace poker
