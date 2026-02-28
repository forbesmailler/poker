#!/usr/bin/env python3
"""Generate engine/generated_config.h from config/solver.yaml."""

import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    # Minimal YAML parser for our simple config format
    yaml = None


def parse_yaml_simple(path: str) -> dict:
    """Minimal YAML parser for flat/nested scalar + list values."""
    result: dict = {}
    current_section = None
    with open(path) as f:
        for line in f:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            if ":" in stripped and not stripped.startswith("-"):
                key, _, value = stripped.partition(":")
                key = key.strip()
                value = value.strip()
                if value == "" or value.startswith("#"):
                    # Section header
                    current_section = key
                    if current_section not in result:
                        result[current_section] = {}
                elif value.startswith("["):
                    # Inline list
                    items = value.strip("[]").split(",")
                    parsed = []
                    for item in items:
                        item = item.strip()
                        if not item:
                            continue
                        try:
                            parsed.append(float(item))
                        except ValueError:
                            parsed.append(item.strip('"').strip("'"))
                    if current_section:
                        result[current_section][key] = parsed
                    else:
                        result[key] = parsed
                elif value in ("true", "false"):
                    val = value == "true"
                    if current_section:
                        result[current_section][key] = val
                    else:
                        result[key] = val
                elif value.startswith('"') or value.startswith("'"):
                    val = value.strip('"').strip("'")
                    if current_section:
                        result[current_section][key] = val
                    else:
                        result[key] = val
                else:
                    # Remove inline comments
                    value = value.split("#")[0].strip()
                    try:
                        val = int(value)
                    except ValueError:
                        try:
                            val = float(value)
                        except ValueError:
                            val = value
                    if current_section:
                        result[current_section][key] = val
                    else:
                        result[key] = val
    return result


def load_config(path: str) -> dict:
    if yaml is not None:
        with open(path) as f:
            return yaml.safe_load(f)
    return parse_yaml_simple(path)


def cpp_value(val) -> str:
    if isinstance(val, bool):
        return "true" if val else "false"
    if isinstance(val, int):
        return str(val)
    if isinstance(val, float):
        return f"{val}f"
    if isinstance(val, str):
        return f'"{val}"'
    return str(val)


def cpp_array(name: str, values: list, elem_type: str) -> str:
    items = ", ".join(cpp_value(v) for v in values)
    return f"constexpr std::array<{elem_type}, {len(values)}> {name} = {{{{{items}}}}};"


def generate_header(config: dict) -> str:
    lines = [
        "#pragma once",
        "// Auto-generated from config/solver.yaml — do not edit manually.",
        "",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace config {",
        "",
        "// === Abstraction ===",
    ]

    abst = config.get("abstraction", {})
    for key in (
        "preflop_buckets",
        "flop_buckets",
        "turn_buckets",
        "river_buckets",
        "equity_samples",
        "histogram_bins",
    ):
        val = abst.get(key, 0)
        lines.append(f"constexpr int {key.upper()} = {cpp_value(val)};")

    lines.append("")
    lines.append("// === Action ===")
    act = config.get("action", {})
    for key in (
        "preflop_raise_sizes",
        "flop_bet_sizes",
        "turn_bet_sizes",
        "river_bet_sizes",
    ):
        values = act.get(key, [])
        lines.append(cpp_array(key.upper(), values, "float"))

    lines.append(
        f"constexpr bool INCLUDE_ALL_IN = {cpp_value(act.get('include_all_in', True))};"
    )
    lines.append(
        f"constexpr int MAX_RAISES_PER_ROUND = {cpp_value(act.get('max_raises_per_round', 4))};"
    )

    lines.append("")
    lines.append("// === Training ===")
    train = config.get("training", {})
    for key in (
        "num_iterations",
        "num_threads",
        "checkpoint_interval",
    ):
        val = train.get(key, 0)
        lines.append(f"constexpr int {key.upper()} = {cpp_value(val)};")

    lines.append(
        f"constexpr const char* CHECKPOINT_DIR = {cpp_value(train.get('checkpoint_dir', 'checkpoints'))};"
    )
    for key in ("dcfr_alpha", "dcfr_beta", "dcfr_gamma"):
        val = train.get(key, 0.0)
        lines.append(f"constexpr float {key.upper()} = {cpp_value(val)};")

    lines.append("")
    lines.append("// === Game ===")
    game = config.get("game", {})
    for key in ("num_players", "small_blind", "big_blind", "starting_stack"):
        val = game.get(key, 0)
        lines.append(f"constexpr int {key.upper()} = {cpp_value(val)};")

    lines.append("")
    lines.append("} // namespace config")
    lines.append("")

    return "\n".join(lines)


def main():
    repo_root = Path(__file__).resolve().parent.parent
    config_path = repo_root / "config" / "solver.yaml"
    output_path = repo_root / "engine" / "generated_config.h"

    if len(sys.argv) > 1:
        config_path = Path(sys.argv[1])
    if len(sys.argv) > 2:
        output_path = Path(sys.argv[2])

    config = load_config(str(config_path))
    header = generate_header(config)

    output_path.parent.mkdir(parents=True, exist_ok=True)

    # Only write if content changed
    if output_path.exists() and output_path.read_text() == header:
        print("generated_config.h is up to date")
        return

    output_path.write_text(header)
    print(f"Generated {output_path}")


if __name__ == "__main__":
    main()
