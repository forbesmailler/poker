#include "trainer.h"
#include "deck.h"
#include "utils.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace poker {

Trainer::Trainer(const CardAbstraction& card_abs,
                  const ActionAbstraction& action_abs)
    : eval_(), store_(256), card_abs_(card_abs), action_abs_(action_abs) {}

void Trainer::train(const TrainingConfig& cfg) {
    should_stop_ = false;
    iteration_counter_ = 0;

    // Create checkpoint directory
    std::filesystem::create_directories(cfg.checkpoint_dir);

    log_info("Starting MCCFR training:");
    log_info("  Iterations: " + std::to_string(cfg.num_iterations));
    log_info("  Threads:    " + std::to_string(cfg.num_threads));

    Timer timer;

    std::vector<std::thread> threads;
    threads.reserve(cfg.num_threads);
    for (int t = 0; t < cfg.num_threads; ++t) {
        threads.emplace_back(&Trainer::worker_thread, this, t, std::cref(cfg));
    }

    // Monitor thread
    while (!should_stop_) {
        int64_t current = iteration_counter_.load();
        if (current >= cfg.num_iterations) break;

        std::this_thread::sleep_for(std::chrono::seconds(5));

        double elapsed = timer.elapsed_seconds();
        size_t num_infosets = store_.size();
        size_t memory_mb = store_.memory_bytes() / (1024 * 1024);

        if (progress_cb_) {
            progress_cb_(static_cast<int>(current), elapsed,
                         num_infosets, memory_mb);
        } else {
            std::ostringstream oss;
            oss << "Iteration " << current << "/" << cfg.num_iterations
                << " | " << elapsed << "s"
                << " | InfoSets: " << num_infosets
                << " | Memory: " << memory_mb << " MB";
            log_info(oss.str());
        }

        // Checkpoint
        if (cfg.checkpoint_interval > 0 &&
            current > 0 &&
            current % cfg.checkpoint_interval == 0) {
            save_checkpoint(static_cast<int>(current), cfg);
        }
    }

    should_stop_ = true;
    for (auto& t : threads) {
        t.join();
    }

    double total_time = timer.elapsed_seconds();
    log_info("Training complete: " + std::to_string(iteration_counter_.load()) +
             " iterations in " + std::to_string(total_time) + "s");

    // Final checkpoint
    save_checkpoint(static_cast<int>(iteration_counter_.load()), cfg);
}

void Trainer::worker_thread(int thread_id, const TrainingConfig& cfg) {
    Rng rng(42 + static_cast<uint64_t>(thread_id) * 1000003);
    MCCFR mccfr(store_, card_abs_, action_abs_, eval_);

    std::array<int32_t, MAX_PLAYERS> stacks;
    stacks.fill(config::STARTING_STACK);

    while (!should_stop_) {
        int64_t iter = iteration_counter_.fetch_add(1);
        if (iter >= cfg.num_iterations) break;

        // DCFR discounting (only thread 0 applies it)
        if (thread_id == 0 && iter > 0 && iter % 10000 == 0) {
            apply_dcfr_discounting(static_cast<int>(iter), cfg);
        }

        // Create a new hand
        int dealer = static_cast<int>(iter % MAX_PLAYERS);
        GameState state = GameState::new_hand(
            stacks, dealer, config::SMALL_BLIND, config::BIG_BLIND
        );

        // Deal hole cards to all players
        Deck deck;
        deck.shuffle(rng);
        for (int p = 0; p < MAX_PLAYERS; ++p) {
            Card c0 = deck.deal();
            Card c1 = deck.deal();
            state.set_hole_cards(p, c0, c1);
        }

        // Pick traversing player (round-robin)
        int traverser = static_cast<int>(iter % MAX_PLAYERS);

        // Run MCCFR traversal
        mccfr.traverse(state, traverser, rng, static_cast<int>(iter));
    }
}

void Trainer::apply_dcfr_discounting(int iteration, const TrainingConfig& cfg) {
    float t = static_cast<float>(iteration);
    float positive_discount =
        std::pow(t, cfg.dcfr_alpha) / (std::pow(t, cfg.dcfr_alpha) + 1.0f);
    float negative_discount =
        cfg.dcfr_beta >= 0
            ? std::pow(t, cfg.dcfr_beta) /
              (std::pow(t, cfg.dcfr_beta) + 1.0f)
            : 0.0f; // negative beta = floor at zero
    float strategy_discount =
        std::pow(t / (t + 1.0f), cfg.dcfr_gamma);

    store_.apply_discounting(positive_discount, negative_discount,
                              strategy_discount);
}

void Trainer::save_checkpoint(int iteration, const TrainingConfig& cfg) {
    std::string path = cfg.checkpoint_dir + "/strategy_" +
                       std::to_string(iteration) + ".bin";
    store_.save(path);
}

void Trainer::stop() {
    should_stop_ = true;
}

const InfoSetStore& Trainer::get_store() const {
    return store_;
}

void Trainer::set_progress_callback(ProgressCallback cb) {
    progress_cb_ = std::move(cb);
}

} // namespace poker
