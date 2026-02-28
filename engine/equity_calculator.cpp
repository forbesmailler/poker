#include "equity_calculator.h"
#include "deck.h"
#include <cmath>
#include <algorithm>

namespace poker {

float EquityCalculator::compute_equity(
    Card hole0, Card hole1,
    const Card* board, int num_board,
    const HandEvaluator& eval,
    int num_samples
) const {
    Rng rng(static_cast<uint64_t>(hole0) * 53 + hole1 + 12345);

    CardMask dead = card_bit(hole0) | card_bit(hole1);
    for (int i = 0; i < num_board; ++i) {
        dead |= card_bit(board[i]);
    }

    int wins = 0, ties = 0, total = 0;

    for (int s = 0; s < num_samples; ++s) {
        // Deal opponent hole cards
        Card opp0 = CARD_NONE, opp1 = CARD_NONE;
        while (true) {
            opp0 = static_cast<Card>(rng.next_int(52));
            if (dead & card_bit(opp0)) continue;
            opp1 = static_cast<Card>(rng.next_int(52));
            if (opp1 == opp0 || (dead & card_bit(opp1))) continue;
            break;
        }

        CardMask sample_dead = dead | card_bit(opp0) | card_bit(opp1);

        // Deal remaining board cards
        Card full_board[5];
        for (int i = 0; i < num_board; ++i) {
            full_board[i] = board[i];
        }
        for (int i = num_board; i < 5; ++i) {
            Card c;
            do {
                c = static_cast<Card>(rng.next_int(52));
            } while (sample_dead & card_bit(c));
            full_board[i] = c;
            sample_dead |= card_bit(c);
        }

        HandRank my_rank = eval.evaluate(
            hole0, hole1,
            full_board[0], full_board[1], full_board[2],
            full_board[3], full_board[4]
        );
        HandRank opp_rank = eval.evaluate(
            opp0, opp1,
            full_board[0], full_board[1], full_board[2],
            full_board[3], full_board[4]
        );

        if (my_rank > opp_rank) wins++;
        else if (my_rank == opp_rank) ties++;
        total++;
    }

    return (static_cast<float>(wins) + 0.5f * static_cast<float>(ties)) /
           static_cast<float>(total);
}

EquityHistogram EquityCalculator::compute_histogram(
    Card hole0, Card hole1,
    const Card* board, int num_board,
    const HandEvaluator& eval,
    int num_bins, int num_samples
) const {
    EquityHistogram hist(num_bins, 0.0f);
    Rng rng(static_cast<uint64_t>(hole0) * 53 + hole1 + 67890);

    CardMask dead = card_bit(hole0) | card_bit(hole1);
    for (int i = 0; i < num_board; ++i) {
        dead |= card_bit(board[i]);
    }

    // For each sample, complete the board and compute equity
    for (int s = 0; s < num_samples; ++s) {
        CardMask sample_dead = dead;

        // Deal remaining board cards for this runout
        Card full_board[5];
        for (int i = 0; i < num_board; ++i) {
            full_board[i] = board[i];
        }
        for (int i = num_board; i < 5; ++i) {
            Card c;
            do {
                c = static_cast<Card>(rng.next_int(52));
            } while (sample_dead & card_bit(c));
            full_board[i] = c;
            sample_dead |= card_bit(c);
        }

        // Compute equity on this runout with a few opponent samples
        int sub_wins = 0, sub_ties = 0, sub_total = 0;
        constexpr int OPPONENT_SAMPLES = 20;

        for (int o = 0; o < OPPONENT_SAMPLES; ++o) {
            Card opp0, opp1;
            CardMask opp_dead = sample_dead;
            do { opp0 = static_cast<Card>(rng.next_int(52)); }
            while (opp_dead & card_bit(opp0));
            opp_dead |= card_bit(opp0);
            do { opp1 = static_cast<Card>(rng.next_int(52)); }
            while (opp_dead & card_bit(opp1));

            HandRank my_rank = eval.evaluate(
                hole0, hole1,
                full_board[0], full_board[1], full_board[2],
                full_board[3], full_board[4]
            );
            HandRank opp_rank = eval.evaluate(
                opp0, opp1,
                full_board[0], full_board[1], full_board[2],
                full_board[3], full_board[4]
            );

            if (my_rank > opp_rank) sub_wins++;
            else if (my_rank == opp_rank) sub_ties++;
            sub_total++;
        }

        float equity = (static_cast<float>(sub_wins) +
                        0.5f * static_cast<float>(sub_ties)) /
                       static_cast<float>(sub_total);

        int bin = std::min(static_cast<int>(equity * num_bins), num_bins - 1);
        hist[bin] += 1.0f;
    }

    // Normalize to probability distribution
    float sum = 0.0f;
    for (float v : hist) sum += v;
    if (sum > 0.0f) {
        for (float& v : hist) v /= sum;
    }

    return hist;
}

float EquityCalculator::emd(const EquityHistogram& a,
                             const EquityHistogram& b) {
    // 1D Earth Mover's Distance = sum of |CDF_a - CDF_b|
    int n = static_cast<int>(std::min(a.size(), b.size()));
    float distance = 0.0f;
    float cdf_diff = 0.0f;

    for (int i = 0; i < n; ++i) {
        cdf_diff += a[i] - b[i];
        distance += std::fabs(cdf_diff);
    }

    return distance;
}

} // namespace poker
