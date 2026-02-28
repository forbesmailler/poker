#include <gtest/gtest.h>
#include <set>
#include "card.h"

using namespace poker;

TEST(Card, MakeCard) {
    // 2c = rank 0, suit 0 → card 0
    EXPECT_EQ(make_card(0, 0), 0);
    // As = rank 12, suit 3 → card 51
    EXPECT_EQ(make_card(12, 3), 51);
    // Td = rank 8, suit 1 → card 33
    EXPECT_EQ(make_card(8, 1), 33);
}

TEST(Card, RankOf) {
    EXPECT_EQ(rank_of(0), 0);   // 2c → rank 0 (deuce)
    EXPECT_EQ(rank_of(51), 12); // As → rank 12 (ace)
    EXPECT_EQ(rank_of(4), 1);   // 3c → rank 1
}

TEST(Card, SuitOf) {
    EXPECT_EQ(suit_of(0), 0);  // clubs
    EXPECT_EQ(suit_of(1), 1);  // diamonds
    EXPECT_EQ(suit_of(2), 2);  // hearts
    EXPECT_EQ(suit_of(3), 3);  // spades
    EXPECT_EQ(suit_of(51), 3); // As → spades
}

TEST(Card, CardToString) {
    EXPECT_EQ(card_to_string(make_card(0, 0)), "2c");
    EXPECT_EQ(card_to_string(make_card(12, 3)), "As");
    EXPECT_EQ(card_to_string(make_card(8, 2)), "Th");
    EXPECT_EQ(card_to_string(make_card(11, 1)), "Kd");
    EXPECT_EQ(card_to_string(CARD_NONE), "??");
}

TEST(Card, StringToCard) {
    EXPECT_EQ(string_to_card("2c"), make_card(0, 0));
    EXPECT_EQ(string_to_card("As"), make_card(12, 3));
    EXPECT_EQ(string_to_card("Th"), make_card(8, 2));
    EXPECT_EQ(string_to_card("Kd"), make_card(11, 1));
}

TEST(Card, StringToCardInvalid) {
    EXPECT_THROW(string_to_card("XX"), std::invalid_argument);
    EXPECT_THROW(string_to_card(""), std::invalid_argument);
    EXPECT_THROW(string_to_card("A"), std::invalid_argument);
}

TEST(Card, CardBit) {
    EXPECT_EQ(card_bit(0), 1ULL);
    EXPECT_EQ(card_bit(1), 2ULL);
    EXPECT_EQ(card_bit(51), 1ULL << 51);
}

TEST(Card, RoundTrip) {
    // Every card can be converted to string and back
    for (int i = 0; i < 52; ++i) {
        Card c = static_cast<Card>(i);
        std::string s = card_to_string(c);
        EXPECT_EQ(string_to_card(s), c) << "Failed for card " << i;
    }
}

TEST(Card, AllCardsUnique) {
    // All 52 cards have unique string representations
    std::set<std::string> seen;
    for (int i = 0; i < 52; ++i) {
        std::string s = card_to_string(static_cast<Card>(i));
        EXPECT_TRUE(seen.insert(s).second) << "Duplicate: " << s;
    }
}
