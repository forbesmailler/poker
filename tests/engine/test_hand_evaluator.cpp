#include <gtest/gtest.h>
#include "hand_evaluator.h"

using namespace poker;

class HandEvaluatorTest : public ::testing::Test {
protected:
    const HandEvaluator& eval = get_evaluator();
};

TEST_F(HandEvaluatorTest, RoyalFlushBeatsStraightFlush) {
    // Royal flush: As Ks Qs Js Ts + garbage
    auto royal = eval.evaluate(
        make_card(12, 3), make_card(11, 3), make_card(10, 3),
        make_card(9, 3), make_card(8, 3), make_card(0, 0), make_card(1, 1));
    // 9-high straight flush: 9s 8s 7s 6s 5s + garbage
    auto sf = eval.evaluate(
        make_card(7, 3), make_card(6, 3), make_card(5, 3),
        make_card(4, 3), make_card(3, 3), make_card(0, 0), make_card(1, 1));
    EXPECT_GT(royal, sf);
}

TEST_F(HandEvaluatorTest, StraightFlushBeatsQuads) {
    // 8-high straight flush: 8h 7h 6h 5h 4h + garbage
    auto sf = eval.evaluate(
        make_card(6, 2), make_card(5, 2), make_card(4, 2),
        make_card(3, 2), make_card(2, 2), make_card(0, 0), make_card(1, 1));
    // Four aces: Ac Ad Ah As + garbage
    auto quads = eval.evaluate(
        make_card(12, 0), make_card(12, 1), make_card(12, 2),
        make_card(12, 3), make_card(11, 0), make_card(0, 1), make_card(1, 1));
    EXPECT_GT(sf, quads);
}

TEST_F(HandEvaluatorTest, QuadsBeatFullHouse) {
    // Four kings: Kc Kd Kh Ks + A
    auto quads = eval.evaluate(
        make_card(11, 0), make_card(11, 1), make_card(11, 2),
        make_card(11, 3), make_card(12, 0), make_card(0, 1), make_card(1, 1));
    // Full house: AAA KK
    auto fh = eval.evaluate(
        make_card(12, 0), make_card(12, 1), make_card(12, 2),
        make_card(11, 0), make_card(11, 1), make_card(0, 2), make_card(1, 2));
    EXPECT_GT(quads, fh);
}

TEST_F(HandEvaluatorTest, FullHouseBeatsFlush) {
    // Full house: AAA KK
    auto fh = eval.evaluate(
        make_card(12, 0), make_card(12, 1), make_card(12, 2),
        make_card(11, 0), make_card(11, 1), make_card(0, 2), make_card(1, 2));
    // Flush: Ah Kh Qh Jh 9h + garbage
    auto flush = eval.evaluate(
        make_card(12, 2), make_card(11, 2), make_card(10, 2),
        make_card(9, 2), make_card(7, 2), make_card(0, 0), make_card(1, 1));
    EXPECT_GT(fh, flush);
}

TEST_F(HandEvaluatorTest, FlushBeatsStraight) {
    // Flush: Ah Kh Qh Jh 9h + garbage
    auto flush = eval.evaluate(
        make_card(12, 2), make_card(11, 2), make_card(10, 2),
        make_card(9, 2), make_card(7, 2), make_card(0, 0), make_card(1, 1));
    // Straight: A K Q J T (rainbow)
    auto straight = eval.evaluate(
        make_card(12, 0), make_card(11, 1), make_card(10, 2),
        make_card(9, 3), make_card(8, 0), make_card(0, 1), make_card(1, 1));
    EXPECT_GT(flush, straight);
}

TEST_F(HandEvaluatorTest, StraightBeatsTrips) {
    // Straight: A K Q J T (rainbow)
    auto straight = eval.evaluate(
        make_card(12, 0), make_card(11, 1), make_card(10, 2),
        make_card(9, 3), make_card(8, 0), make_card(0, 1), make_card(1, 1));
    // Three aces + garbage
    auto trips = eval.evaluate(
        make_card(12, 0), make_card(12, 1), make_card(12, 2),
        make_card(9, 3), make_card(7, 0), make_card(0, 1), make_card(1, 1));
    EXPECT_GT(straight, trips);
}

TEST_F(HandEvaluatorTest, TripsBeatsTwoPair) {
    // Three deuces + garbage
    auto trips = eval.evaluate(
        make_card(0, 0), make_card(0, 1), make_card(0, 2),
        make_card(9, 3), make_card(7, 0), make_card(5, 1), make_card(1, 1));
    // Two pair: AA KK + garbage
    auto two_pair = eval.evaluate(
        make_card(12, 0), make_card(12, 1), make_card(11, 2),
        make_card(11, 3), make_card(7, 0), make_card(5, 1), make_card(1, 1));
    EXPECT_GT(trips, two_pair);
}

TEST_F(HandEvaluatorTest, TwoPairBeatsPair) {
    // Two pair: AA KK
    auto two_pair = eval.evaluate(
        make_card(12, 0), make_card(12, 1), make_card(11, 2),
        make_card(11, 3), make_card(7, 0), make_card(5, 1), make_card(1, 1));
    // One pair: AA + garbage
    auto pair = eval.evaluate(
        make_card(12, 0), make_card(12, 1), make_card(9, 2),
        make_card(7, 3), make_card(5, 0), make_card(3, 1), make_card(1, 1));
    EXPECT_GT(two_pair, pair);
}

TEST_F(HandEvaluatorTest, PairBeatsHighCard) {
    // One pair: 22
    auto pair = eval.evaluate(
        make_card(0, 0), make_card(0, 1), make_card(9, 2),
        make_card(7, 3), make_card(5, 0), make_card(3, 1), make_card(1, 2));
    // High card: A K Q J 9 (not all same suit)
    auto high = eval.evaluate(
        make_card(12, 0), make_card(11, 1), make_card(10, 2),
        make_card(9, 3), make_card(7, 0), make_card(3, 1), make_card(1, 1));
    EXPECT_GT(pair, high);
}

TEST_F(HandEvaluatorTest, CategoryClassification) {
    // Straight flush
    auto sf = eval.evaluate(
        make_card(12, 3), make_card(11, 3), make_card(10, 3),
        make_card(9, 3), make_card(8, 3), make_card(0, 0), make_card(1, 1));
    EXPECT_EQ(HandEvaluator::category(sf), STRAIGHT_FLUSH);

    // Flush
    auto flush = eval.evaluate(
        make_card(12, 2), make_card(11, 2), make_card(10, 2),
        make_card(9, 2), make_card(7, 2), make_card(0, 0), make_card(1, 1));
    EXPECT_EQ(HandEvaluator::category(flush), FLUSH);

    // Straight
    auto straight = eval.evaluate(
        make_card(12, 0), make_card(11, 1), make_card(10, 2),
        make_card(9, 3), make_card(8, 0), make_card(0, 1), make_card(1, 1));
    EXPECT_EQ(HandEvaluator::category(straight), STRAIGHT);
}

TEST_F(HandEvaluatorTest, WheelStraight) {
    // A-2-3-4-5 (wheel) — should be a straight
    auto wheel = eval.evaluate(
        make_card(12, 0), make_card(0, 1), make_card(1, 2),
        make_card(2, 3), make_card(3, 0), make_card(7, 1), make_card(9, 1));
    EXPECT_EQ(HandEvaluator::category(wheel), STRAIGHT);

    // 6-high straight should beat wheel
    auto six_high = eval.evaluate(
        make_card(0, 0), make_card(1, 1), make_card(2, 2),
        make_card(3, 3), make_card(4, 0), make_card(7, 1), make_card(9, 1));
    EXPECT_GT(six_high, wheel);
}

TEST_F(HandEvaluatorTest, FiveCardEvaluation) {
    // Test 5-card evaluation via array
    Card cards[5] = {
        make_card(12, 0), make_card(12, 1), make_card(10, 2),
        make_card(9, 3), make_card(8, 0)
    };
    auto pair_aces = eval.evaluate(cards, 5);
    EXPECT_EQ(HandEvaluator::category(pair_aces), ONE_PAIR);
}

TEST_F(HandEvaluatorTest, HigherPairWins) {
    // Pair of aces
    auto aces = eval.evaluate(
        make_card(12, 0), make_card(12, 1), make_card(10, 2),
        make_card(9, 3), make_card(8, 0), make_card(3, 1), make_card(1, 1));
    // Pair of kings
    auto kings = eval.evaluate(
        make_card(11, 0), make_card(11, 1), make_card(10, 2),
        make_card(9, 3), make_card(8, 0), make_card(3, 1), make_card(1, 1));
    EXPECT_GT(aces, kings);
}
