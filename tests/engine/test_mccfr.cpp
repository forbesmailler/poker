#include <gtest/gtest.h>
#include "information_set.h"
#include "infoset_store.h"
#include "rng.h"

using namespace poker;

TEST(InfoSetData, RegretMatchingUniform) {
    InfoSetData data(3);
    // All zero regrets → uniform strategy
    float strategy[3];
    data.current_strategy(strategy);
    EXPECT_FLOAT_EQ(strategy[0], 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(strategy[1], 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(strategy[2], 1.0f / 3.0f);
}

TEST(InfoSetData, RegretMatchingPositiveOnly) {
    InfoSetData data(3);
    data.cumulative_regret[0] = 10.0f;
    data.cumulative_regret[1] = -5.0f;  // Negative → ignored
    data.cumulative_regret[2] = 20.0f;

    float strategy[3];
    data.current_strategy(strategy);
    EXPECT_FLOAT_EQ(strategy[0], 10.0f / 30.0f);
    EXPECT_FLOAT_EQ(strategy[1], 0.0f);
    EXPECT_FLOAT_EQ(strategy[2], 20.0f / 30.0f);
}

TEST(InfoSetData, AverageStrategyAccumulation) {
    InfoSetData data(2);
    data.strategy_sum[0] = 100.0f;
    data.strategy_sum[1] = 300.0f;

    float avg[2];
    data.average_strategy(avg);
    EXPECT_FLOAT_EQ(avg[0], 0.25f);
    EXPECT_FLOAT_EQ(avg[1], 0.75f);
}

TEST(InfoSetData, AverageStrategyZeroSum) {
    InfoSetData data(3);
    // All zeros → uniform
    float avg[3];
    data.average_strategy(avg);
    EXPECT_FLOAT_EQ(avg[0], 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(avg[1], 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(avg[2], 1.0f / 3.0f);
}

TEST(InfoSetKey, EncodeDecode) {
    InfoSetKey key = make_infoset_key(3, 2, 150, 12345);
    // Player should be in top 3 bits
    int player = static_cast<int>(key >> 61);
    EXPECT_EQ(player, 3);
    // Street in next 2 bits
    int street = static_cast<int>((key >> 59) & 0x3);
    EXPECT_EQ(street, 2);
}

TEST(InfoSetStore, GetOrCreate) {
    InfoSetStore store(16);
    auto& data = store.get_or_create(42, 3);
    EXPECT_EQ(data.num_actions, 3);
    EXPECT_EQ(store.size(), 1u);

    // Getting same key returns same data
    auto& data2 = store.get_or_create(42, 3);
    EXPECT_EQ(&data, &data2);
    EXPECT_EQ(store.size(), 1u);
}

TEST(InfoSetStore, MultipleSets) {
    InfoSetStore store(16);
    for (int i = 0; i < 1000; ++i) {
        store.get_or_create(static_cast<InfoSetKey>(i), 3);
    }
    EXPECT_EQ(store.size(), 1000u);
}

TEST(InfoSetStore, Find) {
    InfoSetStore store(16);
    store.get_or_create(42, 3);

    auto* found = store.find(42);
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->num_actions, 3);

    auto* not_found = store.find(99);
    EXPECT_EQ(not_found, nullptr);
}

TEST(InfoSetStore, Discounting) {
    InfoSetStore store(16);
    auto& data = store.get_or_create(42, 2);
    data.cumulative_regret[0] = 10.0f;
    data.cumulative_regret[1] = -5.0f;
    data.strategy_sum[0] = 100.0f;
    data.strategy_sum[1] = 200.0f;

    store.apply_discounting(0.5f, 0.1f, 0.8f);

    EXPECT_FLOAT_EQ(data.cumulative_regret[0], 5.0f);    // Positive * 0.5
    EXPECT_FLOAT_EQ(data.cumulative_regret[1], -0.5f);   // Negative * 0.1
    EXPECT_FLOAT_EQ(data.strategy_sum[0], 80.0f);        // * 0.8
    EXPECT_FLOAT_EQ(data.strategy_sum[1], 160.0f);       // * 0.8
}

TEST(Rng, SampleActionUniform) {
    Rng rng(42);
    float probs[3] = {1.0f / 3, 1.0f / 3, 1.0f / 3};
    int counts[3] = {};
    int n = 10000;

    for (int i = 0; i < n; ++i) {
        int action = rng.sample_action(probs, 3);
        EXPECT_GE(action, 0);
        EXPECT_LT(action, 3);
        counts[action]++;
    }

    // Each should be roughly 1/3
    for (int i = 0; i < 3; ++i) {
        double ratio = static_cast<double>(counts[i]) / n;
        EXPECT_NEAR(ratio, 1.0 / 3, 0.05);
    }
}

TEST(Rng, SampleActionSkewed) {
    Rng rng(42);
    float probs[3] = {0.7f, 0.2f, 0.1f};
    int counts[3] = {};
    int n = 10000;

    for (int i = 0; i < n; ++i) {
        counts[rng.sample_action(probs, 3)]++;
    }

    EXPECT_NEAR(static_cast<double>(counts[0]) / n, 0.7, 0.05);
    EXPECT_NEAR(static_cast<double>(counts[1]) / n, 0.2, 0.05);
    EXPECT_NEAR(static_cast<double>(counts[2]) / n, 0.1, 0.05);
}
