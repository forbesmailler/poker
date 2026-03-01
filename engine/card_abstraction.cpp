#include "card_abstraction.h"
#include "generated_config.h"
#include "utils.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <numeric>

namespace poker {

CardAbstraction::CardAbstraction() {}

int CardAbstraction::canonical_preflop_index(Card c0, Card c1) {
    uint8_t r0 = rank_of(c0), r1 = rank_of(c1);
    uint8_t s0 = suit_of(c0), s1 = suit_of(c1);

    // Ensure r0 >= r1 (higher rank first)
    if (r0 < r1) {
        std::swap(r0, r1);
        std::swap(s0, s1);
    }

    bool suited = (s0 == s1);

    if (r0 == r1) {
        // Pocket pair: index = sum of ranks below
        // Pairs: 22, 33, ..., AA → indices 0-12
        return r0;
    } else if (suited) {
        // Suited: AKs, AQs, ..., 32s
        // Upper triangle of 13x13 matrix
        // Index = 13 + offset for suited hands
        int idx = 13;
        for (int i = 12; i > r0; --i) {
            idx += i;  // number of suited hands with higher rank = i
        }
        idx += (r0 - r1 - 1);
        return idx;
    } else {
        // Offsuit: AKo, AQo, ..., 32o
        int idx = 13 + 78;  // 78 suited combos
        for (int i = 12; i > r0; --i) {
            idx += i;
        }
        idx += (r0 - r1 - 1);
        return idx;
    }
}

Bucket CardAbstraction::get_bucket(Street street, Card hole0, Card hole1, const Card* board,
                                   int num_board_cards) const {
    if (!built_) {
        // Return a simple bucket based on canonical preflop index
        int idx = canonical_preflop_index(hole0, hole1);
        return static_cast<Bucket>(idx % num_buckets(street));
    }

    switch (street) {
        case Street::PREFLOP: {
            int idx = canonical_preflop_index(hole0, hole1);
            if (idx >= 0 && idx < static_cast<int>(preflop_buckets_.size())) {
                return preflop_buckets_[idx];
            }
            return 0;
        }
        case Street::FLOP: {
            const auto& eval = get_evaluator();
            Card cards[7];
            cards[0] = hole0;
            cards[1] = hole1;
            for (int i = 0; i < num_board_cards; ++i) {
                cards[2 + i] = board[i];
            }
            HandRank rank = eval.evaluate(cards, 2 + num_board_cards);
            return rank_to_bucket_[0][rank];
        }
        case Street::TURN: {
            // Equity rollout: average the river percentile over all 46 possible river cards
            const auto& eval = get_evaluator();
            Card cards[7];
            cards[0] = hole0;
            cards[1] = hole1;
            for (int i = 0; i < num_board_cards; ++i) {
                cards[2 + i] = board[i];
            }
            // Build bitmask of the 6 known cards
            uint64_t used = 0;
            for (int i = 0; i < 2 + num_board_cards; ++i) {
                used |= (1ULL << cards[i]);
            }
            int sum = 0;
            int count = 0;
            const auto& river_table = rank_to_bucket_[2];
            for (int c = 0; c < 52; ++c) {
                if (used & (1ULL << c)) continue;
                cards[6] = static_cast<Card>(c);
                HandRank rank = eval.evaluate(cards, 7);
                sum += river_table[rank];
                ++count;
            }
            // avg in [0, num_river_buckets-1], discretize to bin index
            int bin = static_cast<int>(
                static_cast<double>(sum) / count * TURN_AVG_RESOLUTION + 0.5);
            int max_bin = static_cast<int>(rank_to_bucket_[1].size()) - 1;
            if (bin < 0) bin = 0;
            if (bin > max_bin) bin = max_bin;
            return rank_to_bucket_[1][bin];
        }
        case Street::RIVER: {
            const auto& eval = get_evaluator();
            Card cards[7];
            cards[0] = hole0;
            cards[1] = hole1;
            for (int i = 0; i < num_board_cards; ++i) {
                cards[2 + i] = board[i];
            }
            HandRank rank = eval.evaluate(cards, 2 + num_board_cards);
            return rank_to_bucket_[2][rank];
        }
        default:
            return 0;
    }
}

int CardAbstraction::num_buckets(Street street) const {
    switch (street) {
        case Street::PREFLOP:
            return config::PREFLOP_BUCKETS;
        case Street::FLOP:
            return config::FLOP_BUCKETS;
        case Street::TURN:
            return config::TURN_BUCKETS;
        case Street::RIVER:
            return config::RIVER_BUCKETS;
        default:
            return 1;
    }
}

void CardAbstraction::build(int num_threads, int64_t max_combos_per_street) {
    Timer timer;
    log_info("Building card abstraction...");

    build_preflop_abstraction();
    log_info("  Preflop: " + std::to_string(preflop_buckets_.size()) + " canonical hands");

    const auto& eval = get_evaluator();
    build_postflop_tables(eval, num_threads, max_combos_per_street);

    built_ = true;
    log_info("Card abstraction built in " + std::to_string(timer.elapsed_seconds()) + "s");
}

void CardAbstraction::build_preflop_only() {
    build_preflop_abstraction();
    built_ = true;
}

void CardAbstraction::build_preflop_abstraction() {
    // 169 canonical preflop hands
    // For lossless preflop: each canonical hand IS its own bucket
    preflop_buckets_.resize(169);
    for (int i = 0; i < 169; ++i) {
        preflop_buckets_[i] = static_cast<Bucket>(i);
    }
}

// Helper: build a HandRank→Bucket CDF table by enumerating C(52,n) card combos.
// max_combos: 0 = exhaustive, >0 = stop after this many combos (for fast tests).
static void build_rank_cdf_table(const HandEvaluator& eval, int n, int nb,
                                 int num_threads, std::vector<Bucket>& out_table,
                                 int64_t max_combos) {
    const int table_size = static_cast<int>(MAX_HAND_RANK) + 1;

    std::vector<std::vector<uint64_t>> thread_freq(num_threads,
                                                    std::vector<uint64_t>(table_size, 0));
    std::atomic<int> next_c0{0};
    std::atomic<int64_t> combo_count{0};
    const int max_c0 = 52 - n + 1;
    const bool has_limit = (max_combos > 0);

    auto worker = [&](int tid) {
        auto& freq = thread_freq[tid];
        int64_t local_count = 0;
        while (true) {
            if (has_limit && combo_count.load(std::memory_order_relaxed) >= max_combos) break;
            int c0 = next_c0.fetch_add(1);
            if (c0 >= max_c0) break;

            Card cards[7];
            cards[0] = static_cast<Card>(c0);

            if (n == 5) {
                for (int c1 = c0 + 1; c1 < 49; ++c1) {
                    cards[1] = static_cast<Card>(c1);
                    for (int c2 = c1 + 1; c2 < 50; ++c2) {
                        cards[2] = static_cast<Card>(c2);
                        for (int c3 = c2 + 1; c3 < 51; ++c3) {
                            cards[3] = static_cast<Card>(c3);
                            for (int c4 = c3 + 1; c4 < 52; ++c4) {
                                cards[4] = static_cast<Card>(c4);
                                ++freq[eval.evaluate(cards, 5)];
                                if (has_limit && (++local_count & 0xFFFF) == 0) {
                                    combo_count.fetch_add(0x10000, std::memory_order_relaxed);
                                    if (combo_count.load(std::memory_order_relaxed) >= max_combos) goto done;
                                }
                            }
                        }
                    }
                }
            } else {  // n == 7
                for (int c1 = c0 + 1; c1 < 47; ++c1) {
                    cards[1] = static_cast<Card>(c1);
                    for (int c2 = c1 + 1; c2 < 48; ++c2) {
                        cards[2] = static_cast<Card>(c2);
                        for (int c3 = c2 + 1; c3 < 49; ++c3) {
                            cards[3] = static_cast<Card>(c3);
                            for (int c4 = c3 + 1; c4 < 50; ++c4) {
                                cards[4] = static_cast<Card>(c4);
                                for (int c5 = c4 + 1; c5 < 51; ++c5) {
                                    cards[5] = static_cast<Card>(c5);
                                    for (int c6 = c5 + 1; c6 < 52; ++c6) {
                                        cards[6] = static_cast<Card>(c6);
                                        ++freq[eval.evaluate(cards, 7)];
                                        if (has_limit && (++local_count & 0xFFFF) == 0) {
                                            combo_count.fetch_add(0x10000, std::memory_order_relaxed);
                                            if (combo_count.load(std::memory_order_relaxed) >= max_combos) goto done;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        done:;
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) threads.emplace_back(worker, t);
    for (auto& t : threads) t.join();

    // Merge
    std::vector<uint64_t> freq(table_size, 0);
    for (int t = 0; t < num_threads; ++t)
        for (int i = 0; i < table_size; ++i)
            freq[i] += thread_freq[t][i];

    // CDF → bucket
    out_table.resize(table_size);
    uint64_t total = 0;
    for (int i = 0; i < table_size; ++i) total += freq[i];

    uint64_t cumulative = 0;
    Bucket last_bucket = 0;
    for (int i = 0; i < table_size; ++i) {
        if (freq[i] > 0) {
            cumulative += freq[i];
            Bucket b = static_cast<Bucket>(
                std::min(static_cast<uint64_t>(nb - 1),
                         cumulative * static_cast<uint64_t>(nb) / total));
            out_table[i] = b;
            last_bucket = b;
        } else {
            out_table[i] = last_bucket;
        }
    }
}

void CardAbstraction::build_postflop_tables(const HandEvaluator& eval, int num_threads,
                                             int64_t max_combos) {
    if (num_threads < 1) num_threads = 1;
    const bool has_limit = (max_combos > 0);

    // --- 1. Flop: 5-card hand rank percentile ---
    {
        Timer t;
        build_rank_cdf_table(eval, 5, config::FLOP_BUCKETS, num_threads,
                             rank_to_bucket_[0], max_combos);
        log_info("  Flop (5 cards) built in " + std::to_string(t.elapsed_seconds()) + "s");
    }

    // --- 2. River: 7-card hand rank percentile (must be built before turn) ---
    {
        Timer t;
        build_rank_cdf_table(eval, 7, config::RIVER_BUCKETS, num_threads,
                             rank_to_bucket_[2], max_combos);
        log_info("  River (7 cards) built in " + std::to_string(t.elapsed_seconds()) + "s");
    }

    // --- 3. Turn: equity rollout (avg river percentile over 46 river cards) ---
    {
        Timer t;
        const int nb = config::TURN_BUCKETS;
        const int num_bins = (config::RIVER_BUCKETS - 1) * TURN_AVG_RESOLUTION + 1;

        std::vector<std::vector<uint64_t>> thread_freq(num_threads,
                                                        std::vector<uint64_t>(num_bins, 0));
        std::atomic<int> next_c0{0};
        std::atomic<int64_t> combo_count{0};

        const auto& river_table = rank_to_bucket_[2];

        auto worker = [&](int tid) {
            auto& freq = thread_freq[tid];
            int64_t local_count = 0;
            while (true) {
                if (has_limit && combo_count.load(std::memory_order_relaxed) >= max_combos) break;
                int c0 = next_c0.fetch_add(1);
                if (c0 > 46) break;

                Card cards[7];
                cards[0] = static_cast<Card>(c0);

                for (int c1 = c0 + 1; c1 < 48; ++c1) {
                    cards[1] = static_cast<Card>(c1);
                    for (int c2 = c1 + 1; c2 < 49; ++c2) {
                        cards[2] = static_cast<Card>(c2);
                        for (int c3 = c2 + 1; c3 < 50; ++c3) {
                            cards[3] = static_cast<Card>(c3);
                            for (int c4 = c3 + 1; c4 < 51; ++c4) {
                                cards[4] = static_cast<Card>(c4);
                                for (int c5 = c4 + 1; c5 < 52; ++c5) {
                                    cards[5] = static_cast<Card>(c5);

                                    uint64_t used = (1ULL << c0) | (1ULL << c1) |
                                                    (1ULL << c2) | (1ULL << c3) |
                                                    (1ULL << c4) | (1ULL << c5);

                                    int sum = 0;
                                    for (int c6 = 0; c6 < 52; ++c6) {
                                        if (used & (1ULL << c6)) continue;
                                        cards[6] = static_cast<Card>(c6);
                                        HandRank r = eval.evaluate(cards, 7);
                                        sum += river_table[r];
                                    }

                                    int bin = static_cast<int>(
                                        static_cast<double>(sum) / 46.0 * TURN_AVG_RESOLUTION + 0.5);
                                    if (bin >= num_bins) bin = num_bins - 1;
                                    ++freq[bin];

                                    if (has_limit && (++local_count & 0x3FFF) == 0) {
                                        combo_count.fetch_add(0x4000, std::memory_order_relaxed);
                                        if (combo_count.load(std::memory_order_relaxed) >= max_combos) return;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        };

        std::vector<std::thread> threads;
        for (int th = 0; th < num_threads; ++th) threads.emplace_back(worker, th);
        for (auto& th : threads) th.join();

        // Merge
        std::vector<uint64_t> freq(num_bins, 0);
        for (int th = 0; th < num_threads; ++th)
            for (int i = 0; i < num_bins; ++i)
                freq[i] += thread_freq[th][i];

        // CDF → bucket
        rank_to_bucket_[1].resize(num_bins);
        uint64_t total = 0;
        for (int i = 0; i < num_bins; ++i) total += freq[i];

        uint64_t cumulative = 0;
        Bucket last_bucket = 0;
        for (int i = 0; i < num_bins; ++i) {
            if (freq[i] > 0) {
                cumulative += freq[i];
                Bucket b = static_cast<Bucket>(
                    std::min(static_cast<uint64_t>(nb - 1),
                             cumulative * static_cast<uint64_t>(nb) / total));
                rank_to_bucket_[1][i] = b;
                last_bucket = b;
            } else {
                rank_to_bucket_[1][i] = last_bucket;
            }
        }

        log_info("  Turn (6 cards, equity rollout): " + std::to_string(total) +
                 " combos enumerated in " + std::to_string(t.elapsed_seconds()) + "s");
    }
}

static constexpr uint8_t ABSTRACTION_VERSION = 3;

void CardAbstraction::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        log_error("Failed to save abstraction to: " + path);
        return;
    }

    write_binary(out, ABSTRACTION_VERSION);
    write_vector_binary(out, preflop_buckets_);
    for (int s = 0; s < 3; ++s) {
        write_vector_binary(out, rank_to_bucket_[s]);
    }

    log_info("Saved card abstraction to " + path);
}

void CardAbstraction::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        log_error("Failed to load abstraction from: " + path);
        return;
    }

    uint8_t version;
    read_binary(in, version);

    if (version == 3) {
        read_vector_binary(in, preflop_buckets_);
        for (int s = 0; s < 3; ++s) {
            read_vector_binary(in, rank_to_bucket_[s]);
        }
    } else {
        log_error("Unknown abstraction version: " + std::to_string(version));
        return;
    }

    built_ = true;
    log_info("Loaded card abstraction (v" + std::to_string(version) + ") from " + path);
}

}  // namespace poker
