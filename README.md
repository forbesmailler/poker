# Poker GTO Solver

A 6-max No-Limit Hold'em GTO solver using Monte Carlo Counterfactual Regret Minimization (MCCFR) with external sampling and Discounted CFR (Pluribus-style).

## Prerequisites

| Dependency | Version | Notes |
|---|---|---|
| **C++ compiler** | C++17 | MSVC (VS2022) on Windows, GCC 9+ or Clang 10+ on Linux/macOS |
| **CMake** | 3.20+ | Build system |
| **Git** | any | Google Test is fetched at build time |
| **Conda/Mamba** | any | Python environment manager ([miniforge](https://github.com/conda-forge/miniforge) recommended) |

### Installing prerequisites

**Windows:**
Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with the "Desktop development with C++" workload (includes CMake and MSVC). Install [miniforge](https://github.com/conda-forge/miniforge#miniforge3).

**macOS:**
```bash
xcode-select --install          # Apple Clang
brew install cmake miniforge
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt update
sudo apt install build-essential cmake git
# Install miniforge: https://github.com/conda-forge/miniforge#miniforge3
```

## Setup

Clone the repo and create the Python environment:

```bash
git clone <repo-url> poker
cd poker
mamba env create -f environment.yaml
mamba activate poker
```

The conda environment provides Python 3.14, `invoke` (task runner), `ruff` (formatter), and `pytest`.

## Building

### Quick build (all-in-one)

```bash
invoke prepare
```

This runs: format → generate config header → CMake build → tests.

### Step-by-step build

```bash
# 1. Generate C++ config header from config/solver.yaml
invoke gen_config

# 2. Build the C++ engine (Release mode)
invoke build
```

Or using raw CMake commands:

```bash
python scripts/gen_config_header.py
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --config Release
```

The solver executable is produced at:

| Platform | Path |
|---|---|
| Windows | `engine/build/Release/poker_solver.exe` |
| Linux/macOS | `engine/build/poker_solver` |

### Running tests

```bash
invoke test_cpp    # 62 C++ tests via CTest
invoke test_py     # Python tests via pytest
invoke test        # Both
```

Or directly:

```bash
ctest --test-dir engine/build -C Release --output-on-failure
```

## Generating `strategy_100000008.bin`

This is the full end-to-end workflow to train a GTO strategy from scratch.

### 1. Configure training parameters

Edit `config/solver.yaml` to set your desired parameters:

```yaml
abstraction:
  preflop_buckets: 169        # Lossless (suit isomorphism) — don't change
  flop_buckets: 50
  turn_buckets: 50
  river_buckets: 50
  equity_samples: 1000
  histogram_bins: 50

action:
  preflop_raise_sizes: [0.75, 1.5]    # Pot-fraction bet sizes
  flop_bet_sizes: [0.5, 1.0]
  turn_bet_sizes: [0.5, 1.0]
  river_bet_sizes: [0.75, 1.5]
  include_all_in: true
  max_raises_per_round: 3

training:
  num_iterations: 100000000   # 100M — produces strategy_100000008.bin
  num_threads: 8              # Set to your CPU core count
  checkpoint_interval: 1000000
  checkpoint_dir: "checkpoints"
  dcfr_alpha: 1.5
  dcfr_beta: 0.0
  dcfr_gamma: 2.0

game:
  num_players: 6
  small_blind: 1
  big_blind: 2
  starting_stack: 200         # 100 BB
```

Key things to tune:
- **`num_threads`** — set to your CPU core count for best throughput.
- **`num_iterations`** — 100M iterations produces the final `strategy_100000008.bin`. Reduce for faster test runs.
- **`checkpoint_interval`** — how often intermediate snapshots are saved (default: every 1M iterations).

After editing, regenerate the C++ config header and rebuild:

```bash
invoke gen_config
invoke build
```

### 2. Build card abstraction (one-time)

```bash
# Windows
engine/build/Release/poker_solver.exe build-abstraction

# Linux/macOS
engine/build/poker_solver build-abstraction
```

This precomputes card bucket assignments for flop/turn/river and saves them to `checkpoints/abstraction.bin`. Only needs to be run once (or again if you change abstraction settings).

### 3. Train

```bash
# Windows
engine/build/Release/poker_solver.exe train

# Linux/macOS
engine/build/poker_solver train
```

Or use the invoke shortcut which formats, builds, tests, then trains:

```bash
invoke train
```

Training will:
1. Load the card abstraction (or build it if `abstraction.bin` doesn't exist).
2. Run MCCFR with `num_threads` worker threads.
3. Apply DCFR discounting periodically.
4. Save checkpoint files to `checkpoints/` every `checkpoint_interval` iterations:
   `strategy_1000000.bin`, `strategy_2000000.bin`, ..., `strategy_100000008.bin`.

The final file is `strategy_100000008.bin` (the iteration count may be slightly above 100M due to multi-threaded batching — this is expected).

### 4. Resume from checkpoint

Training is **automatically resumable**. If the process is interrupted, just run `train` again — it finds the latest `strategy_*.bin` checkpoint and continues from where it left off.

### Resource requirements

- **RAM:** ~8–10 GB at 100M iterations.
- **Disk:** The final `strategy_100000008.bin` is ~7.6 GB. Intermediate checkpoints add to this.
- **Time:** Depends heavily on CPU. Expect hours to days for 100M iterations.

## CLI Reference

```
poker_solver <command>

Commands:
  info                Show configuration and checkpoint count
  build-abstraction   Precompute card abstraction tables
  train               Run MCCFR training (auto-resumes from latest checkpoint)
  query               Interactive strategy lookup by info set key
```

### `info` — Show configuration

```bash
poker_solver info
```

Prints game parameters, abstraction settings, DCFR parameters, and how many checkpoint files exist.

### `query` — Look up strategies

```bash
poker_solver query
```

Loads the latest checkpoint and drops into an interactive prompt. Enter a uint64 info set key to see the average (converged) strategy as action percentages. Type `quit` to exit.

## Analyzing Results

Use the Python analysis script to inspect a strategy file:

```bash
python scripts/analyze_strategy.py checkpoints/strategy_100000008.bin
```

This shows:
- Total info set count
- Breakdown by street (preflop/flop/turn/river)
- Average number of actions per street
- Sample preflop strategies
- Memory estimate

If run without arguments, it auto-detects the latest checkpoint in `checkpoints/`.

## Project Structure

```
poker/
├── config/
│   └── solver.yaml                 # Training/game/abstraction parameters
├── engine/
│   ├── CMakeLists.txt              # CMake build config
│   ├── main.cpp                    # CLI entry point
│   ├── trainer.h/.cpp              # Multi-threaded training orchestration
│   ├── mccfr.h/.cpp                # External sampling MCCFR traversal
│   ├── infoset_store.h/.cpp        # 256-shard concurrent hashmap + serialization
│   ├── information_set.h/.cpp      # Info set key encoding + regret/strategy data
│   ├── card_abstraction.h/.cpp     # Card bucketing (169 preflop, hash post-flop)
│   ├── action_abstraction.h/.cpp   # Pot-fraction bet sizing per street
│   ├── game_state.h/.cpp           # 6-player NLHE game tree
│   ├── pot_manager.h/.cpp          # Side pot tracking
│   ├── hand_evaluator.h/.cpp       # Lookup-table 5/6/7-card evaluator
│   ├── equity_calculator.h/.cpp    # Hand vs distribution equity
│   ├── card.h/.cpp                 # Card representation
│   ├── deck.h/.cpp                 # Deck + dealing
│   ├── rng.h                       # xoshiro256** RNG
│   ├── action.h                    # Action types (fold/check/call/bet)
│   ├── utils.h/.cpp                # Logging, timers, binary I/O
│   └── generated_config.h          # Auto-generated (not in git)
├── scripts/
│   ├── gen_config_header.py        # YAML → C++ header generator
│   └── analyze_strategy.py         # Strategy file analysis
├── tests/engine/                   # Google Test files (62 tests)
├── checkpoints/                    # Training output (not in git)
├── environment.yaml                # Conda environment
└── tasks.py                        # Invoke task definitions
```

## Development

```bash
invoke format      # ruff (Python) + clang-format (C++)
invoke test        # All tests
invoke prepare     # Format → gen_config → build → test
invoke all         # Format + Python tests
```

## Quick Start (copy-paste)

```bash
# Full pipeline from zero to trained strategy
mamba env create -f environment.yaml
mamba activate poker
invoke prepare
engine/build/Release/poker_solver.exe build-abstraction   # Windows
engine/build/Release/poker_solver.exe train               # Windows
# Linux/macOS: replace with engine/build/poker_solver
```
