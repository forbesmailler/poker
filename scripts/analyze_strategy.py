#!/usr/bin/env python3
"""Read binary strategy files and display human-readable strategy info."""

import struct
import sys
from pathlib import Path
from collections import defaultdict


def read_strategy_file(path: str) -> dict:
    """Read a binary strategy file produced by InfoSetStore::save()."""
    infosets = {}
    with open(path, "rb") as f:
        (total_entries,) = struct.unpack("<Q", f.read(8))
        print(f"Total info sets: {total_entries:,}")

        for _ in range(total_entries):
            (key,) = struct.unpack("<Q", f.read(8))
            (num_actions,) = struct.unpack("<B", f.read(1))

            regrets = struct.unpack(f"<{num_actions}f", f.read(4 * num_actions))
            strategy_sum = struct.unpack(
                f"<{num_actions}f", f.read(4 * num_actions)
            )

            infosets[key] = {
                "num_actions": num_actions,
                "regrets": list(regrets),
                "strategy_sum": list(strategy_sum),
            }

    return infosets


def decode_key(key: int) -> dict:
    """Decode an InfoSetKey into its components."""
    player = (key >> 61) & 0x7
    street = (key >> 59) & 0x3
    card_bucket = (key >> 43) & 0xFFFF
    action_hash = key & 0x7FFFFFFFFFF

    street_names = ["Preflop", "Flop", "Turn", "River"]
    return {
        "player": player,
        "street": street_names[street] if street < 4 else f"Unknown({street})",
        "card_bucket": card_bucket,
        "action_hash": action_hash,
    }


def average_strategy(strategy_sum: list[float]) -> list[float]:
    """Compute average strategy from accumulated strategy sums."""
    total = sum(strategy_sum)
    if total > 0:
        return [s / total for s in strategy_sum]
    n = len(strategy_sum)
    return [1.0 / n] * n


def analyze(path: str):
    """Analyze a strategy file."""
    infosets = read_strategy_file(path)

    # Stats by street
    street_counts: dict[str, int] = defaultdict(int)
    street_action_counts: dict[str, list[int]] = defaultdict(list)

    for key, data in infosets.items():
        decoded = decode_key(key)
        street = decoded["street"]
        street_counts[street] += 1
        street_action_counts[street].append(data["num_actions"])

    print(f"\n{'='*50}")
    print(f"Strategy Analysis: {path}")
    print(f"{'='*50}")
    print(f"Total info sets: {len(infosets):,}")
    print(f"Estimated memory: {len(infosets) * 100 / 1024 / 1024:.1f} MB")

    print(f"\nInfo sets by street:")
    for street in ["Preflop", "Flop", "Turn", "River"]:
        count = street_counts.get(street, 0)
        if count > 0:
            avg_actions = sum(street_action_counts[street]) / len(
                street_action_counts[street]
            )
            print(f"  {street:8s}: {count:>10,}  (avg {avg_actions:.1f} actions)")

    # Show sample strategies
    print(f"\nSample preflop strategies:")
    count = 0
    for key, data in infosets.items():
        decoded = decode_key(key)
        if decoded["street"] != "Preflop":
            continue

        avg = average_strategy(data["strategy_sum"])
        action_names = ["Fold", "Check", "Call"] + [
            f"Bet{i}" for i in range(data["num_actions"] - 3)
        ]
        if len(action_names) < data["num_actions"]:
            action_names.extend(
                [f"A{i}" for i in range(len(action_names), data["num_actions"])]
            )

        strat_str = "  ".join(
            f"{action_names[i][:4]}:{avg[i]*100:5.1f}%"
            for i in range(data["num_actions"])
        )
        print(
            f"  P{decoded['player']} bucket={decoded['card_bucket']:3d}: {strat_str}"
        )

        count += 1
        if count >= 20:
            break


def main():
    if len(sys.argv) < 2:
        print("Usage: python analyze_strategy.py <strategy_file.bin>")
        print("\nLooks for files in checkpoints/ by default.")

        checkpoint_dir = Path("checkpoints")
        if checkpoint_dir.exists():
            files = sorted(checkpoint_dir.glob("strategy_*.bin"))
            if files:
                print(f"\nFound {len(files)} checkpoint(s):")
                for f in files[-5:]:
                    size_mb = f.stat().st_size / 1024 / 1024
                    print(f"  {f.name} ({size_mb:.1f} MB)")
                print(f"\nAnalyzing latest: {files[-1]}")
                analyze(str(files[-1]))
        return

    analyze(sys.argv[1])


if __name__ == "__main__":
    main()
