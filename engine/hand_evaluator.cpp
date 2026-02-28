#include "hand_evaluator.h"
#include <algorithm>
#include <cstring>
#include <cassert>

namespace poker {

// Rank a 5-card hand using bit manipulation
// Returns a 16-bit rank where higher = better
// Top 4 bits = category, bottom 12 bits = sub-ranking

// Utility: count bits set in a 16-bit value
static int popcount16(uint16_t x) {
    x = x - ((x >> 1) & 0x5555);
    x = (x & 0x3333) + ((x >> 2) & 0x3333);
    x = (x + (x >> 4)) & 0x0f0f;
    return (x + (x >> 8)) & 0x1f;
}

// Check if rank_bits form a straight, return high card rank or -1
static int is_straight(uint16_t rank_bits) {
    // rank_bits: bit 0 = rank 0 (deuce), bit 12 = rank 12 (ace)
    // Check for 5 consecutive bits
    // Also check wheel (A-2-3-4-5): bits 0,1,2,3,12

    // Check wheel first
    if ((rank_bits & 0x100F) == 0x100F) {  // A,2,3,4,5
        return 3;                          // 5-high straight (high card is 5, rank=3)
    }

    for (int top = 12; top >= 4; --top) {
        uint16_t mask = 0x1F << (top - 4);
        if ((rank_bits & mask) == mask) {
            return top;
        }
    }
    return -1;
}

static HandRank make_rank(int category, int sub) {
    return static_cast<HandRank>((category << 12) | (sub & 0xFFF));
}

HandEvaluator::HandEvaluator() {
    init_tables();
}

void HandEvaluator::init_tables() {
    init_flush_table();
    init_unique5_table();
    init_remaining_table();
}

void HandEvaluator::init_flush_table() {
    // For each possible 13-bit rank pattern with >= 5 bits set,
    // evaluate as a flush hand
    std::memset(flush_table_.data(), 0, sizeof(flush_table_));

    for (uint16_t bits = 0; bits < 8192; ++bits) {
        int count = popcount16(bits);
        if (count < 5)
            continue;

        // For flush + unique ranks, only meaningful if exactly 5
        // For 6-7 card evaluation, we extract the best 5
        if (count != 5)
            continue;

        int straight_high = is_straight(bits);
        if (straight_high >= 0) {
            // Straight flush
            flush_table_[bits] = make_rank(STRAIGHT_FLUSH, straight_high);
        } else {
            // Regular flush — rank by top 5 cards
            int sub = 0;
            int shift = 0;
            for (int r = 0; r < 13; ++r) {
                if (bits & (1 << r)) {
                    sub |= (r << (shift * 4));
                    shift++;
                }
            }
            // Encode top cards for proper ordering
            // Use the bit pattern itself as sub-rank (higher bits = higher ranks)
            flush_table_[bits] = make_rank(FLUSH, bits);
        }
    }
}

void HandEvaluator::init_unique5_table() {
    // For 5 unique ranks (no pairs), evaluate as high card or straight
    std::memset(unique5_table_.data(), 0, sizeof(unique5_table_));

    for (uint16_t bits = 0; bits < 8192; ++bits) {
        if (popcount16(bits) != 5)
            continue;

        int straight_high = is_straight(bits);
        if (straight_high >= 0) {
            unique5_table_[bits] = make_rank(STRAIGHT, straight_high);
        } else {
            unique5_table_[bits] = make_rank(HIGH_CARD, bits);
        }
    }
}

void HandEvaluator::init_remaining_table() {
    // For hands with pairs/trips/quads, use product-of-primes hash
    std::memset(hash_table_.data(), 0, sizeof(hash_table_));
    std::memset(hash_keys_.data(), 0, sizeof(hash_keys_));

    // Enumerate all C(13,5) rank combinations with repetitions
    // (where at least two cards share a rank)
    // Generate all 5-card rank multisets
    for (int r0 = 0; r0 < 13; ++r0) {
        for (int r1 = r0; r1 < 13; ++r1) {
            for (int r2 = r1; r2 < 13; ++r2) {
                for (int r3 = r2; r3 < 13; ++r3) {
                    for (int r4 = r3; r4 < 13; ++r4) {
                        int ranks[5] = {r0, r1, r2, r3, r4};

                        // Compute rank bit pattern
                        uint16_t rank_bits = 0;
                        for (int i = 0; i < 5; ++i)
                            rank_bits |= (1 << ranks[i]);

                        int unique_count = popcount16(rank_bits);
                        if (unique_count == 5)
                            continue;  // All unique — handled by unique5_table

                        // Count occurrences of each rank
                        int counts[13] = {};
                        for (int i = 0; i < 5; ++i)
                            counts[ranks[i]]++;

                        // Classify hand
                        int max_count = 0;
                        int second_max = 0;
                        for (int i = 0; i < 13; ++i) {
                            if (counts[i] > max_count) {
                                second_max = max_count;
                                max_count = counts[i];
                            } else if (counts[i] > second_max) {
                                second_max = counts[i];
                            }
                        }

                        int category;
                        if (max_count == 4)
                            category = FOUR_OF_A_KIND;
                        else if (max_count == 3 && second_max == 2)
                            category = FULL_HOUSE;
                        else if (max_count == 3)
                            category = THREE_OF_A_KIND;
                        else if (max_count == 2 && second_max == 2)
                            category = TWO_PAIR;
                        else
                            category = ONE_PAIR;

                        // Sub-ranking: encode the important ranks
                        // For ordering, put the primary group rank highest
                        int sub = 0;

                        // Collect ranks by count (descending count, then descending rank)
                        struct RankInfo {
                            int rank;
                            int count;
                        };
                        RankInfo infos[5];
                        int n_infos = 0;
                        for (int r = 12; r >= 0; --r) {
                            if (counts[r] > 0) {
                                infos[n_infos++] = {r, counts[r]};
                            }
                        }
                        // Sort by count descending, then rank descending
                        std::sort(infos, infos + n_infos, [](const RankInfo& a, const RankInfo& b) {
                            if (a.count != b.count)
                                return a.count > b.count;
                            return a.rank > b.rank;
                        });

                        // Encode: primary rank * 169 + secondary * 13 + tertiary
                        // This gives a unique ordering within each category
                        if (n_infos >= 1)
                            sub = infos[0].rank;
                        if (n_infos >= 2)
                            sub = sub * 13 + infos[1].rank;
                        if (n_infos >= 3)
                            sub = sub * 13 + infos[2].rank;
                        if (n_infos >= 4)
                            sub = sub * 13 + infos[3].rank;

                        HandRank hand_rank = make_rank(category, sub & 0xFFF);

                        // Compute prime product hash
                        uint32_t prime_product = 1;
                        for (int i = 0; i < 5; ++i) {
                            prime_product *= static_cast<uint32_t>(RANK_PRIMES[ranks[i]]);
                        }

                        // Insert into hash table (open addressing)
                        uint32_t idx = prime_product % HASH_TABLE_SIZE;
                        while (hash_keys_[idx] != 0 && hash_keys_[idx] != prime_product) {
                            idx = (idx + 1) % HASH_TABLE_SIZE;
                        }
                        hash_keys_[idx] = prime_product;
                        hash_table_[idx] = hand_rank;
                    }
                }
            }
        }
    }
}

uint32_t HandEvaluator::hash_find(uint32_t key) const {
    uint32_t idx = key % HASH_TABLE_SIZE;
    while (hash_keys_[idx] != key) {
        idx = (idx + 1) % HASH_TABLE_SIZE;
    }
    return idx;
}

HandRank HandEvaluator::lookup_flush(uint16_t rank_bits) const {
    return flush_table_[rank_bits];
}

HandRank HandEvaluator::lookup_unique5(uint16_t rank_bits) const {
    return unique5_table_[rank_bits];
}

HandRank HandEvaluator::eval5(Card c0, Card c1, Card c2, Card c3, Card c4) const {
    uint8_t r0 = rank_of(c0), r1 = rank_of(c1), r2 = rank_of(c2);
    uint8_t r3 = rank_of(c3), r4 = rank_of(c4);
    uint8_t s0 = suit_of(c0), s1 = suit_of(c1), s2 = suit_of(c2);
    uint8_t s3 = suit_of(c3), s4 = suit_of(c4);

    uint16_t rank_bits = (1 << r0) | (1 << r1) | (1 << r2) | (1 << r3) | (1 << r4);
    int unique_ranks = popcount16(rank_bits);

    bool is_flush = (s0 == s1 && s1 == s2 && s2 == s3 && s3 == s4);

    if (is_flush) {
        return flush_table_[rank_bits];
    }

    if (unique_ranks == 5) {
        return unique5_table_[rank_bits];
    }

    // Paired hand — use prime product hash
    uint32_t prime_product =
        static_cast<uint32_t>(RANK_PRIMES[r0]) * static_cast<uint32_t>(RANK_PRIMES[r1]) *
        static_cast<uint32_t>(RANK_PRIMES[r2]) * static_cast<uint32_t>(RANK_PRIMES[r3]) *
        static_cast<uint32_t>(RANK_PRIMES[r4]);

    return hash_table_[hash_find(prime_product)];
}

HandRank HandEvaluator::evaluate(Card c0, Card c1, Card c2, Card c3, Card c4, Card c5,
                                 Card c6) const {
    Card cards[7] = {c0, c1, c2, c3, c4, c5, c6};
    return evaluate(cards, 7);
}

HandRank HandEvaluator::evaluate(const Card* cards, int n) const {
    if (n == 5) {
        return eval5(cards[0], cards[1], cards[2], cards[3], cards[4]);
    }

    // For 6 or 7 cards: check for flush first (optimization),
    // then enumerate all C(n,5) combinations and return best
    HandRank best = 0;

    // Check for flush
    int suit_counts[4] = {};
    uint16_t suit_bits[4] = {};
    for (int i = 0; i < n; ++i) {
        uint8_t s = suit_of(cards[i]);
        uint8_t r = rank_of(cards[i]);
        suit_counts[s]++;
        suit_bits[s] |= (1 << r);
    }

    for (int s = 0; s < 4; ++s) {
        if (suit_counts[s] >= 5) {
            // We have a flush in suit s
            // Try all C(count, 5) combinations of cards in this suit
            // For simplicity, enumerate 5-bit subsets of suit_bits[s]
            uint16_t bits = suit_bits[s];

            // Extract individual rank bits
            int flush_ranks[8];  // at most 7
            int nf = 0;
            for (int r = 0; r < 13; ++r) {
                if (bits & (1 << r)) {
                    flush_ranks[nf++] = r;
                }
            }

            // Try all C(nf, 5) combinations
            for (int a = 0; a < nf; ++a) {
                for (int b = a + 1; b < nf; ++b) {
                    for (int c = b + 1; c < nf; ++c) {
                        for (int d = c + 1; d < nf; ++d) {
                            for (int e = d + 1; e < nf; ++e) {
                                uint16_t sub_bits = (1 << flush_ranks[a]) | (1 << flush_ranks[b]) |
                                                    (1 << flush_ranks[c]) | (1 << flush_ranks[d]) |
                                                    (1 << flush_ranks[e]);
                                HandRank r = flush_table_[sub_bits];
                                if (r > best)
                                    best = r;
                            }
                        }
                    }
                }
            }
        }
    }

    // Also check all C(n, 5) for non-flush hands
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            for (int c = b + 1; c < n; ++c) {
                for (int d = c + 1; d < n; ++d) {
                    for (int e = d + 1; e < n; ++e) {
                        HandRank r = eval5(cards[a], cards[b], cards[c], cards[d], cards[e]);
                        if (r > best)
                            best = r;
                    }
                }
            }
        }
    }

    return best;
}

const char* HandEvaluator::category_name(int cat) {
    static const char* names[] = {"High Card",       "One Pair",       "Two Pair",
                                  "Three of a Kind", "Straight",       "Flush",
                                  "Full House",      "Four of a Kind", "Straight Flush"};
    if (cat >= 0 && cat <= 8)
        return names[cat];
    return "Unknown";
}

const HandEvaluator& get_evaluator() {
    static const HandEvaluator instance;
    return instance;
}

}  // namespace poker
