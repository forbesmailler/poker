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
        case Street::FLOP:
        case Street::TURN:
        case Street::RIVER: {
            const auto& eval = get_evaluator();
            Card cards[7];
            cards[0] = hole0;
            cards[1] = hole1;
            for (int i = 0; i < num_board_cards; ++i) {
                cards[2 + i] = board[i];
            }
            HandRank rank = eval.evaluate(cards, 2 + num_board_cards);
            int table_idx = static_cast<int>(street) - 1;  // FLOP=0, TURN=1, RIVER=2
            return rank_to_bucket_[table_idx][rank];
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

void CardAbstraction::build(int num_threads) {
    Timer timer;
    log_info("Building card abstraction...");

    build_preflop_abstraction();
    log_info("  Preflop: " + std::to_string(preflop_buckets_.size()) + " canonical hands");

    const auto& eval = get_evaluator();
    build_postflop_tables(eval, num_threads);

    built_ = true;
    log_info("Card abstraction built in " + std::to_string(timer.elapsed_seconds()) + "s");
}

void CardAbstraction::build_preflop_abstraction() {
    // 169 canonical preflop hands
    // For lossless preflop: each canonical hand IS its own bucket
    preflop_buckets_.resize(169);
    for (int i = 0; i < 169; ++i) {
        preflop_buckets_[i] = static_cast<Bucket>(i);
    }
}

void CardAbstraction::build_postflop_tables(const HandEvaluator& eval, int num_threads) {
    if (num_threads < 1) num_threads = 1;

    const int table_size = static_cast<int>(MAX_HAND_RANK) + 1;

    // Street configs: {num_cards, num_buckets, name}
    struct StreetConfig {
        int num_cards;
        int buckets;
        const char* name;
    };
    const StreetConfig streets[3] = {
        {5, config::FLOP_BUCKETS, "Flop"},
        {6, config::TURN_BUCKETS, "Turn"},
        {7, config::RIVER_BUCKETS, "River"},
    };

    for (int s = 0; s < 3; ++s) {
        Timer street_timer;
        const int n = streets[s].num_cards;
        const int nb = streets[s].buckets;

        // Thread-local frequency arrays
        std::vector<std::vector<uint64_t>> thread_freq(num_threads,
                                                        std::vector<uint64_t>(table_size, 0));

        // Work-stealing over outermost card index
        std::atomic<int> next_c0{0};
        const int max_c0 = 52 - n + 1;  // outermost card ranges [0, 52-n]

        auto worker = [&](int tid) {
            auto& freq = thread_freq[tid];
            while (true) {
                int c0 = next_c0.fetch_add(1);
                if (c0 >= max_c0) break;

                // Enumerate remaining cards with nested loops
                // All cards are in strictly ascending order: c0 < c1 < c2 < ... < c(n-1)
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
                                    HandRank r = eval.evaluate(cards, 5);
                                    ++freq[r];
                                }
                            }
                        }
                    }
                } else if (n == 6) {
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
                                        HandRank r = eval.evaluate(cards, 6);
                                        ++freq[r];
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
                                            HandRank r = eval.evaluate(cards, 7);
                                            ++freq[r];
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        };

        // Launch threads
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back(worker, t);
        }
        for (auto& t : threads) {
            t.join();
        }

        // Merge frequency arrays
        std::vector<uint64_t> freq(table_size, 0);
        for (int t = 0; t < num_threads; ++t) {
            for (int i = 0; i < table_size; ++i) {
                freq[i] += thread_freq[t][i];
            }
        }

        // Build CDF and assign buckets
        rank_to_bucket_[s].resize(table_size);
        uint64_t total = 0;
        for (int i = 0; i < table_size; ++i) {
            total += freq[i];
        }

        uint64_t cumulative = 0;
        Bucket last_bucket = 0;
        for (int i = 0; i < table_size; ++i) {
            if (freq[i] > 0) {
                cumulative += freq[i];
                // bucket = floor(cumulative / total * num_buckets), clamped to [0, nb-1]
                Bucket b = static_cast<Bucket>(
                    std::min(static_cast<uint64_t>(nb - 1),
                             cumulative * static_cast<uint64_t>(nb) / total));
                rank_to_bucket_[s][i] = b;
                last_bucket = b;
            } else {
                // Carry forward last valid bucket for empty HandRank slots
                rank_to_bucket_[s][i] = last_bucket;
            }
        }

        log_info("  " + std::string(streets[s].name) + " (" + std::to_string(n) +
                 " cards): " + std::to_string(total) + " combos enumerated in " +
                 std::to_string(street_timer.elapsed_seconds()) + "s");
    }
}

static constexpr uint8_t ABSTRACTION_VERSION = 2;

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

    if (version == 2) {
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
