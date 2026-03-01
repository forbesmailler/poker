#include "trainer.h"
#include "deck.h"
#include "utils.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <regex>
#include <sstream>

namespace poker {

Trainer::Trainer(const CardAbstraction& card_abs, const ActionAbstraction& action_abs)
    : eval_(), store_(256), card_abs_(card_abs), action_abs_(action_abs) {}

int Trainer::load_latest_checkpoint(const std::string& checkpoint_dir) {
    if (!std::filesystem::exists(checkpoint_dir))
        return 0;

    std::string latest_path;
    int max_iter = 0;
    std::regex pattern("strategy_(\\d+)\\.bin");

    for (const auto& entry : std::filesystem::directory_iterator(checkpoint_dir)) {
        std::string filename = entry.path().filename().string();
        std::smatch match;
        if (std::regex_match(filename, match, pattern)) {
            int iter = std::stoi(match[1].str());
            if (iter > max_iter) {
                max_iter = iter;
                latest_path = entry.path().string();
            }
        }
    }

    if (latest_path.empty())
        return 0;

    log_info("Resuming from checkpoint: " + latest_path);
    store_.load(latest_path);
    return max_iter;
}

void Trainer::train(const TrainingConfig& cfg) {
    should_stop_ = false;

    // Create checkpoint directory
    std::filesystem::create_directories(cfg.checkpoint_dir);

    // Try to resume from latest checkpoint
    int start_iter = load_latest_checkpoint(cfg.checkpoint_dir);
    iteration_counter_ = start_iter;

    int64_t total_target = static_cast<int64_t>(start_iter) + cfg.num_iterations;
    target_iterations_ = total_target;

    log_info("Starting MCCFR training:");
    if (start_iter > 0) {
        log_info("  Resumed at:  " + std::to_string(start_iter));
    }
    log_info("  Target:      " + std::to_string(total_target) + " (" +
             std::to_string(cfg.num_iterations) + " new)");
    log_info("  Threads:     " + std::to_string(cfg.num_threads));

    Timer timer;
    int64_t last_checkpoint = start_iter;

    std::vector<std::thread> threads;
    threads.reserve(cfg.num_threads);
    for (int t = 0; t < cfg.num_threads; ++t) {
        threads.emplace_back(&Trainer::worker_thread, this, t, std::cref(cfg));
    }

    // Monitor thread
    while (!should_stop_) {
        int64_t current = iteration_counter_.load();
        if (current >= total_target)
            break;

        std::this_thread::sleep_for(std::chrono::seconds(5));

        double elapsed = timer.elapsed_seconds();
        size_t num_infosets = store_.size();
        size_t memory_mb = store_.memory_bytes() / (1024 * 1024);

        if (progress_cb_) {
            progress_cb_(static_cast<int>(current), elapsed, num_infosets, memory_mb);
        } else {
            std::ostringstream oss;
            oss << "Iteration " << current << "/" << total_target << " | " << elapsed << "s"
                << " | InfoSets: " << num_infosets << " | Memory: " << memory_mb << " MB";
            log_info(oss.str());
        }

        // Checkpoint when crossing a checkpoint_interval boundary
        if (cfg.checkpoint_interval > 0 && current > 0 &&
            current / cfg.checkpoint_interval > last_checkpoint / cfg.checkpoint_interval) {
            int64_t cp_iter = (current / cfg.checkpoint_interval) * cfg.checkpoint_interval;
            save_checkpoint(static_cast<int>(cp_iter), cfg);
            last_checkpoint = current;
        }
    }

    should_stop_ = true;
    for (auto& t : threads) {
        t.join();
    }

    double total_time = timer.elapsed_seconds();
    log_info("Training complete: " + std::to_string(iteration_counter_.load()) + " iterations in " +
             std::to_string(total_time) + "s");

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
        if (iter >= target_iterations_)
            break;

        // DCFR discounting (only thread 0 applies it)
        if (thread_id == 0 && iter > 0 && iter % 10000 == 0) {
            apply_dcfr_discounting(static_cast<int>(iter), cfg);
        }

        // Create a new hand
        int dealer = static_cast<int>(iter % MAX_PLAYERS);
        GameState state =
            GameState::new_hand(stacks, dealer, config::SMALL_BLIND, config::BIG_BLIND);

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
    float positive_discount = std::pow(t, cfg.dcfr_alpha) / (std::pow(t, cfg.dcfr_alpha) + 1.0f);
    float negative_discount = cfg.dcfr_beta >= 0
                                  ? std::pow(t, cfg.dcfr_beta) / (std::pow(t, cfg.dcfr_beta) + 1.0f)
                                  : 0.0f;  // negative beta = floor at zero
    float strategy_discount = std::pow(t / (t + 1.0f), cfg.dcfr_gamma);

    store_.apply_discounting(positive_discount, negative_discount, strategy_discount);
}

void Trainer::save_checkpoint(int iteration, const TrainingConfig& cfg) {
    std::string path = cfg.checkpoint_dir + "/strategy_" + std::to_string(iteration) + ".bin";
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

}  // namespace poker
