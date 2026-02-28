#pragma once

#include "mccfr.h"
#include "generated_config.h"
#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#include <string>

namespace poker {

struct TrainingConfig {
    int num_iterations = config::NUM_ITERATIONS;
    int num_threads = config::NUM_THREADS;
    int checkpoint_interval = config::CHECKPOINT_INTERVAL;
    std::string checkpoint_dir = config::CHECKPOINT_DIR;
    float dcfr_alpha = config::DCFR_ALPHA;
    float dcfr_beta = config::DCFR_BETA;
    float dcfr_gamma = config::DCFR_GAMMA;
};

class Trainer {
   public:
    Trainer(const CardAbstraction& card_abs, const ActionAbstraction& action_abs);

    void train(const TrainingConfig& cfg = TrainingConfig{});
    void stop();
    const InfoSetStore& get_store() const;

    using ProgressCallback = std::function<void(int iteration, double elapsed_seconds,
                                                size_t num_infosets, size_t memory_mb)>;
    void set_progress_callback(ProgressCallback cb);

   private:
    HandEvaluator eval_;
    InfoSetStore store_;
    const CardAbstraction& card_abs_;
    const ActionAbstraction& action_abs_;
    std::atomic<bool> should_stop_{false};
    std::atomic<int64_t> iteration_counter_{0};
    ProgressCallback progress_cb_;

    void worker_thread(int thread_id, const TrainingConfig& cfg);
    void apply_dcfr_discounting(int iteration, const TrainingConfig& cfg);
    void save_checkpoint(int iteration, const TrainingConfig& cfg);
};

}  // namespace poker
