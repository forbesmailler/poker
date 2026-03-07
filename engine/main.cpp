#include "generated_config.h"
#include "card.h"
#include "card_abstraction.h"
#include "action_abstraction.h"
#include "trainer.h"
#include "infoset_store.h"
#include "hand_evaluator.h"
#include "range_manager.h"
#include "subgame_cfr.h"
#include "interactive_trainer.h"
#include "utils.h"

#include <iostream>
#include <sstream>
#include <string>
#include <filesystem>

using namespace poker;

void print_usage() {
    std::cout << "Usage: poker_solver <command>\n"
              << "\nCommands:\n"
              << "  build-abstraction   Precompute card abstraction tables\n"
              << "  train               Run MCCFR training\n"
              << "  query               Interactive strategy query\n"
              << "  solve               Subgame solving (flop/turn/river, reads from stdin)\n"
              << "  play                Interactive GTO trainer\n"
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

// Find the latest strategy checkpoint file
static std::string find_latest_checkpoint(const std::string& checkpoint_dir) {
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
    return latest;
}

int cmd_query() {
    std::string checkpoint_dir = config::CHECKPOINT_DIR;
    if (!std::filesystem::exists(checkpoint_dir)) {
        log_error("No checkpoint directory found. Run 'train' first.");
        return 1;
    }

    std::string latest = find_latest_checkpoint(checkpoint_dir);
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

int cmd_solve() {
    std::string checkpoint_dir = config::CHECKPOINT_DIR;
    if (!std::filesystem::exists(checkpoint_dir)) {
        log_error("No checkpoint directory found. Run 'train' first.");
        return 1;
    }

    // Load blueprint
    std::string latest = find_latest_checkpoint(checkpoint_dir);
    if (latest.empty()) {
        log_error("No strategy checkpoints found. Run 'train' first.");
        return 1;
    }

    InfoSetStore blueprint;
    blueprint.load(latest);
    log_info("Loaded blueprint from " + latest + " (" + std::to_string(blueprint.size()) +
             " info sets)");

    // Load card abstraction
    CardAbstraction card_abs;
    std::string abs_path = std::string(config::CHECKPOINT_DIR) + "/abstraction.bin";
    if (std::filesystem::exists(abs_path)) {
        card_abs.load(abs_path);
    } else {
        log_error("No abstraction.bin found. Run 'build-abstraction' first.");
        return 1;
    }

    ActionAbstraction action_abs;
    const HandEvaluator& eval = get_evaluator();

    // Parse input from stdin
    int hero_seat = -1, opp_seat = -1;
    Card hero_c0 = CARD_NONE, hero_c1 = CARD_NONE;
    std::vector<Card> board_cards;
    std::array<int32_t, MAX_PLAYERS> stacks = {};
    int dealer_pos = 0;
    int num_iterations = 1000;

    struct ActionRecord {
        int player;
        std::string action_type;
        int amount;
    };
    std::vector<ActionRecord> action_records;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;

        if (keyword == "hero") {
            iss >> hero_seat;
            std::string c0s, c1s;
            iss >> c0s >> c1s;
            hero_c0 = string_to_card(c0s);
            hero_c1 = string_to_card(c1s);
        } else if (keyword == "opponent") {
            iss >> opp_seat;
        } else if (keyword == "board") {
            std::string cs;
            while (iss >> cs) {
                board_cards.push_back(string_to_card(cs));
            }
        } else if (keyword == "stacks") {
            for (int i = 0; i < MAX_PLAYERS; ++i)
                iss >> stacks[i];
        } else if (keyword == "dealer") {
            iss >> dealer_pos;
        } else if (keyword == "action") {
            ActionRecord rec;
            iss >> rec.player >> rec.action_type;
            rec.amount = 0;
            if (rec.action_type == "bet" || rec.action_type == "raise")
                iss >> rec.amount;
            action_records.push_back(rec);
        } else if (keyword == "iterations") {
            iss >> num_iterations;
        } else if (keyword == "end" || keyword == "solve") {
            break;
        }
    }

    if (hero_seat < 0 || opp_seat < 0 || hero_c0 == CARD_NONE || board_cards.size() < 3) {
        log_error("Incomplete input. Need: hero, opponent, board (3-5 cards), stacks, actions.");
        return 1;
    }

    // Reconstruct game state by replaying actions
    GameState state =
        GameState::new_hand(stacks, dealer_pos, config::SMALL_BLIND, config::BIG_BLIND);
    state.set_hole_cards(hero_seat, hero_c0, hero_c1);

    // Track action history for range construction
    std::vector<std::pair<GameState, Action>> action_history;

    // Process actions, dealing board cards when chance nodes arise
    int board_dealt = 0;
    for (const auto& rec : action_records) {
        // Deal board cards if we hit a chance node
        while (state.is_chance_node() && !state.is_terminal()) {
            if (state.street() == Street::FLOP && board_dealt < 3) {
                state = state.deal_flop(board_cards[0], board_cards[1], board_cards[2]);
                board_dealt = 3;
            } else if (state.street() == Street::TURN && board_dealt < 4) {
                state = state.deal_turn(board_cards[3]);
                board_dealt = 4;
            } else if (state.street() == Street::RIVER && board_dealt < 5) {
                state = state.deal_river(board_cards[4]);
                board_dealt = 5;
            } else {
                break;
            }
        }

        Action action;
        if (rec.action_type == "fold")
            action = Action::fold();
        else if (rec.action_type == "check")
            action = Action::check();
        else if (rec.action_type == "call")
            action = Action::call();
        else if (rec.action_type == "bet" || rec.action_type == "raise")
            action = Action::bet(rec.amount);
        else {
            log_error("Unknown action type: " + rec.action_type);
            return 1;
        }

        action_history.push_back({state, action});
        state = state.apply_action(action);
    }

    // Deal remaining board cards
    while (state.is_chance_node() && !state.is_terminal()) {
        if (state.street() == Street::FLOP && board_dealt < 3) {
            state = state.deal_flop(board_cards[0], board_cards[1], board_cards[2]);
            board_dealt = 3;
        } else if (state.street() == Street::TURN && board_dealt < 4) {
            state = state.deal_turn(board_cards[3]);
            board_dealt = 4;
        } else if (state.street() == Street::RIVER && board_dealt < 5) {
            state = state.deal_river(board_cards[4]);
            board_dealt = 5;
        } else {
            break;
        }
    }

    if (state.street() != Street::FLOP && state.street() != Street::TURN &&
        state.street() != Street::RIVER) {
        log_error("State must be on flop, turn, or river for subgame solving. Current street: " +
                  std::to_string(static_cast<int>(state.street())));
        return 1;
    }

    bool depth_limited = (state.street() == Street::FLOP);
    std::string street_name = depth_limited                      ? "Flop (depth-limited)"
                              : (state.street() == Street::TURN) ? "Turn+River"
                                                                 : "River";
    std::cout << street_name << " subgame solving...\n"
              << "  Hero:     seat " << hero_seat << " [" << card_to_string(hero_c0) << " "
              << card_to_string(hero_c1) << "]\n"
              << "  Opponent: seat " << opp_seat << "\n"
              << "  Board:    ";
    for (const auto& c : board_cards)
        std::cout << card_to_string(c) << " ";
    std::cout << "\n  Pot:      " << state.pot() << "\n"
              << "  Iterations: " << num_iterations << "\n\n";

    Timer timer;

    // Build opponent range via Bayesian filtering
    RangeManager range_mgr(blueprint, card_abs, action_abs);
    Range opp_range =
        range_mgr.build_opponent_range(opp_seat, action_history, hero_c0, hero_c1,
                                       board_cards.data(), static_cast<int>(board_cards.size()));

    // Count live combos in opponent range
    int live_combos = 0;
    for (int i = 0; i < Range::NUM_COMBOS; ++i)
        if (opp_range.weights[i] > 0.0f)
            live_combos++;
    std::cout << "Opponent range: " << live_combos << " live combos\n";

    // Build finer action abstraction for subgame solving
    // More bet sizes than the blueprint for better resolution
    std::vector<BetSize> subgame_flop_sizes = {
        {0.33f, false}, {0.5f, false}, {0.75f, false}, {1.0f, false}, {0.0f, true}};
    std::vector<BetSize> subgame_turn_sizes = {{0.25f, false}, {0.5f, false}, {0.75f, false},
                                               {1.0f, false},  {1.5f, false}, {0.0f, true}};
    std::vector<BetSize> subgame_river_sizes = {{0.25f, false}, {0.5f, false}, {0.75f, false},
                                                {1.0f, false},  {1.5f, false}, {0.0f, true}};
    std::vector<BetSize> dummy_sizes = {{0.75f, false}, {0.0f, true}};
    ActionAbstraction subgame_abs(dummy_sizes, subgame_flop_sizes, subgame_turn_sizes,
                                  subgame_river_sizes);

    // Solve subgame
    // For depth-limited flop solving, pass blueprint + card abstraction for leaf value estimation
    SubgameCFR solver(subgame_abs, eval, depth_limited ? &blueprint : nullptr,
                      depth_limited ? &card_abs : nullptr, depth_limited ? &action_abs : nullptr);
    double ev = solver.solve(state, hero_c0, hero_c1, opp_range, hero_seat, opp_seat,
                             num_iterations, depth_limited);

    // Get and print strategy
    auto actions = subgame_abs.get_actions(state);
    int num_actions = static_cast<int>(actions.size());
    float strategy[SubgameNodeData::MAX_ACTIONS];
    solver.get_strategy(state, hero_c0, hero_c1, strategy, num_actions);

    std::cout << "\nHero strategy:\n";
    for (int a = 0; a < num_actions; ++a) {
        std::string action_str;
        switch (actions[a].type) {
            case ActionType::FOLD:
                action_str = "fold";
                break;
            case ActionType::CHECK:
                action_str = "check";
                break;
            case ActionType::CALL:
                action_str = "call";
                break;
            case ActionType::BET:
                action_str = "bet " + std::to_string(actions[a].amount);
                break;
        }
        std::cout << "  " << action_str << ": " << (strategy[a] * 100.0f) << "%\n";
    }
    std::cout << "\nHero EV: " << ev << "\n";
    std::cout << "Solve time: " << timer.elapsed_ms() << " ms\n";

    return 0;
}

int cmd_play() {
    std::string checkpoint_dir = config::CHECKPOINT_DIR;
    if (!std::filesystem::exists(checkpoint_dir)) {
        log_error("No checkpoint directory found. Run 'train' first.");
        return 1;
    }

    std::string latest = find_latest_checkpoint(checkpoint_dir);
    if (latest.empty()) {
        log_error("No strategy checkpoints found. Run 'train' first.");
        return 1;
    }

    // Force flush so loading messages appear immediately
    std::cout << std::unitbuf;

    log_info("Loading blueprint from " + latest + " (this may take a minute for large files)...");
    std::cout.flush();

    InfoSetStore blueprint;
    blueprint.load(latest);
    log_info("Loaded " + std::to_string(blueprint.size()) + " info sets (" +
             std::to_string(blueprint.memory_bytes() / (1024 * 1024)) + " MB)");

    CardAbstraction card_abs;
    std::string abs_path = std::string(config::CHECKPOINT_DIR) + "/abstraction.bin";
    if (std::filesystem::exists(abs_path)) {
        log_info("Loading card abstraction...");
        card_abs.load(abs_path);
    } else {
        log_info("No abstraction.bin found, building now (one-time, may take several minutes)...");
        card_abs.build(config::NUM_THREADS);
        std::filesystem::create_directories(config::CHECKPOINT_DIR);
        card_abs.save(abs_path);
        log_info("Saved abstraction to " + abs_path);
    }

    ActionAbstraction action_abs;

    InteractiveTrainer trainer(blueprint, card_abs, action_abs);
    trainer.run();

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
    if (command == "solve")
        return cmd_solve();
    if (command == "play")
        return cmd_play();

    std::cerr << "Unknown command: " << command << "\n";
    print_usage();
    return 1;
}
