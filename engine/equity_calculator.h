#pragma once

#include "card.h"
#include "hand_evaluator.h"
#include "rng.h"
#include <vector>

namespace poker {

using EquityHistogram = std::vector<float>;

class EquityCalculator {
   public:
    // Equity of (hole0, hole1) against uniform random opponent given board
    // Returns equity in [0, 1]
    float compute_equity(Card hole0, Card hole1, const Card* board, int num_board,
                         const HandEvaluator& eval, int num_samples = 1000) const;

    // Distribution of equity across future board runouts
    // Used for flop/turn clustering
    EquityHistogram compute_histogram(Card hole0, Card hole1, const Card* board, int num_board,
                                      const HandEvaluator& eval, int num_bins = 50,
                                      int num_samples = 1000) const;

    // Earth Mover's Distance between two histograms
    static float emd(const EquityHistogram& a, const EquityHistogram& b);
};

}  // namespace poker
