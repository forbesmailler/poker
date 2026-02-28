#include "card_abstraction.h"
#include "generated_config.h"
#include "utils.h"
#include <algorithm>
#include <cmath>

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
        case Street::RIVER:
            // For post-flop, use a hash-based bucket assignment
            // In a full implementation, these would be precomputed
            {
                uint64_t hash = 0;
                hash ^= static_cast<uint64_t>(hole0) * 2654435761ULL;
                hash ^= static_cast<uint64_t>(hole1) * 40503ULL;
                for (int i = 0; i < num_board_cards; ++i) {
                    hash ^= static_cast<uint64_t>(board[i]) * (2654435761ULL + i * 12345ULL);
                }
                return static_cast<Bucket>(hash % num_buckets(street));
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

void CardAbstraction::build(int /*num_threads*/) {
    Timer timer;
    log_info("Building card abstraction...");

    build_preflop_abstraction();
    log_info("  Preflop: " + std::to_string(preflop_buckets_.size()) + " canonical hands");

    // Post-flop abstractions are expensive — skip if not needed yet
    // build_flop_abstraction(num_threads);
    // build_turn_abstraction(num_threads);
    // build_river_abstraction(num_threads);

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

void CardAbstraction::build_flop_abstraction(int /*num_threads*/) {
    // Compute equity histograms for all canonical flop situations
    // Then cluster into flop_buckets using K-means with EMD distance
    // For now, placeholder — full implementation would enumerate
    // all canonical (hole_cards, flop) combinations
    log_info("  Flop abstraction: placeholder (hash-based)");
}

void CardAbstraction::build_turn_abstraction(int /*num_threads*/) {
    log_info("  Turn abstraction: placeholder (hash-based)");
}

void CardAbstraction::build_river_abstraction(int /*num_threads*/) {
    log_info("  River abstraction: placeholder (hash-based)");
}

void CardAbstraction::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        log_error("Failed to save abstraction to: " + path);
        return;
    }

    write_vector_binary(out, preflop_buckets_);
    write_vector_binary(out, flop_buckets_);
    write_vector_binary(out, turn_buckets_);
    write_vector_binary(out, river_buckets_);

    log_info("Saved card abstraction to " + path);
}

void CardAbstraction::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        log_error("Failed to load abstraction from: " + path);
        return;
    }

    read_vector_binary(in, preflop_buckets_);
    read_vector_binary(in, flop_buckets_);
    read_vector_binary(in, turn_buckets_);
    read_vector_binary(in, river_buckets_);
    built_ = true;

    log_info("Loaded card abstraction from " + path);
}

}  // namespace poker
