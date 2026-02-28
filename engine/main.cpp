#include "generated_config.h"
#include "card.h"
#include "card_abstraction.h"
#include "action_abstraction.h"
#include "trainer.h"
#include "infoset_store.h"
#include "hand_evaluator.h"
#include "utils.h"

#include <iostream>
#include <string>
#include <filesystem>

using namespace poker;

void print_usage() {
    std::cout << "Usage: poker_solver <command>\n"
              << "\nCommands:\n"
              << "  build-abstraction   Precompute card abstraction tables\n"
              << "  train               Run MCCFR training\n"
              << "  query               Interactive strategy query\n"
              << "  info                Show training stats / memory usage\n";
}

int cmd_info() {
    std::cout << "Poker Solver Configuration:\n"
              << "  Players:      " << config::NUM_PLAYERS << "\n"
              << "  Blinds:       " << config::SMALL_BLIND << "/" << config::BIG_BLIND << "\n"
              << "  Stack:        " << config::STARTING_STACK << " ("
              << config::STARTING_STACK / config::BIG_BLIND << " BB)\n"
              << "  Iterations:   " << config::NUM_ITERATIONS << "\n"
              << "  Threads:      " << config::NUM_THREADS << "\n"
              << "\nAbstraction:\n"
              << "  Preflop buckets: " << config::PREFLOP_BUCKETS << "\n"
              << "  Flop buckets:    " << config::FLOP_BUCKETS << "\n"
              << "  Turn buckets:    " << config::TURN_BUCKETS << "\n"
              << "  River buckets:   " << config::RIVER_BUCKETS << "\n"
              << "\nDCFR:\n"
              << "  Alpha: " << config::DCFR_ALPHA << "\n"
              << "  Beta:  " << config::DCFR_BETA << "\n"
              << "  Gamma: " << config::DCFR_GAMMA << "\n";

    // Check for existing checkpoints
    if (std::filesystem::exists(config::CHECKPOINT_DIR)) {
        int count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(config::CHECKPOINT_DIR)) {
            if (entry.path().extension() == ".bin")
                count++;
        }
        std::cout << "\nCheckpoints: " << count << " files in " << config::CHECKPOINT_DIR << "/\n";
    }

    return 0;
}

int cmd_build_abstraction() {
    CardAbstraction abs;
    abs.build(config::NUM_THREADS);

    std::filesystem::create_directories(config::CHECKPOINT_DIR);
    std::string path = std::string(config::CHECKPOINT_DIR) + "/abstraction.bin";
    abs.save(path);

    return 0;
}

int cmd_train() {
    CardAbstraction card_abs;

    // Try to load precomputed abstraction
    std::string abs_path = std::string(config::CHECKPOINT_DIR) + "/abstraction.bin";
    if (std::filesystem::exists(abs_path)) {
        card_abs.load(abs_path);
    } else {
        log_info("No precomputed abstraction found, building...");
        card_abs.build(config::NUM_THREADS);
    }

    ActionAbstraction action_abs;
    Trainer trainer(card_abs, action_abs);

    TrainingConfig cfg;
    trainer.train(cfg);

    return 0;
}

int cmd_query() {
    // Load the latest checkpoint
    std::string checkpoint_dir = config::CHECKPOINT_DIR;
    if (!std::filesystem::exists(checkpoint_dir)) {
        log_error("No checkpoint directory found. Run 'train' first.");
        return 1;
    }

    // Find latest checkpoint
    std::string latest;
    int max_iter = 0;
    for (const auto& entry : std::filesystem::directory_iterator(checkpoint_dir)) {
        std::string filename = entry.path().filename().string();
        if (filename.find("strategy_") == 0 && entry.path().extension() == ".bin") {
            int iter = std::stoi(filename.substr(9, filename.size() - 13));
            if (iter > max_iter) {
                max_iter = iter;
                latest = entry.path().string();
            }
        }
    }

    if (latest.empty()) {
        log_error("No strategy checkpoints found. Run 'train' first.");
        return 1;
    }

    InfoSetStore store;
    store.load(latest);

    std::cout << "Loaded strategy from " << latest << "\n"
              << "Info sets: " << store.size() << "\n"
              << "Memory: " << store.memory_bytes() / (1024 * 1024) << " MB\n\n";

    std::cout << "Strategy query mode (type 'quit' to exit).\n"
              << "Enter info set key (uint64): ";

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit" || line == "exit")
            break;

        try {
            uint64_t key = std::stoull(line);
            const auto* data = store.find(key);
            if (data) {
                float avg[InfoSetData::MAX_ACTIONS];
                data->average_strategy(avg);
                std::cout << "Average strategy (" << (int)data->num_actions << " actions):\n";
                for (int a = 0; a < data->num_actions; ++a) {
                    std::cout << "  Action " << a << ": " << (avg[a] * 100.0f) << "%\n";
                }
            } else {
                std::cout << "Info set not found.\n";
            }
        } catch (...) {
            std::cout << "Invalid key. Enter a uint64 number.\n";
        }

        std::cout << "\nEnter info set key: ";
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];

    if (command == "info")
        return cmd_info();
    if (command == "build-abstraction")
        return cmd_build_abstraction();
    if (command == "train")
        return cmd_train();
    if (command == "query")
        return cmd_query();

    std::cerr << "Unknown command: " << command << "\n";
    print_usage();
    return 1;
}
