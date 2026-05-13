#!/usr/bin/env python3
"""Benchmark adaptation tiers for the Mahjong AI.

Usage:
    python scripts/benchmark.py                          # tier 0 only, 50 games
    python scripts/benchmark.py --tiers 0,1,2,3 -n 50   # compare all tiers
    python scripts/benchmark.py --tiers 0,1 -n 100 --weights assets/model
"""

import argparse
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "mcp"))
from game_wrapper import MahjongGame


def main():
    parser = argparse.ArgumentParser(description="Benchmark Mahjong AI adaptation tiers")
    parser.add_argument("--tiers", default="0", help="Comma-separated adaptation tiers (default: 0)")
    parser.add_argument("-n", "--num-games", type=int, default=50, help="Games per tier (default: 50)")
    parser.add_argument("--weights", default="", help="Path to weights dir (for tiers > 0)")
    args = parser.parse_args()

    tiers = [int(t.strip()) for t in args.tiers.split(",")]
    results = []

    for tier in tiers:
        weights = args.weights if tier > 0 else ""
        if tier > 0 and not weights:
            print(f"Warning: tier {tier} requires --weights; skipping")
            continue
        print(f"\n{'='*60}")
        print(f"  Tier {tier} -- {args.num_games} games")
        print(f"{'='*60}")
        r = MahjongGame.run_benchmark(args.num_games, tier, weights)
        results.append(r)

    # Summary table
    print(f"\n{'='*60}")
    print(f"  RESULTS SUMMARY")
    print(f"{'='*60}")
    print(f"{'Tier':>6} {'Games':>6} {'Rounds':>7} {'AI Win%':>8} {'Human Win%':>11} {'Draw%':>7}")
    print(f"{'-'*6:>6} {'-'*6:>6} {'-'*7:>7} {'-'*8:>8} {'-'*11:>11} {'-'*7:>7}")
    for r in results:
        print(f"{r['adaptationTier']:>6} {r['games']:>6} {r['rounds']:>7} "
              f"{r['aiWinRate']:>7.1f}% {r['humanWinRate']:>10.1f}% {r['drawRate']:>6.1f}%")

    # Faan distribution
    print(f"\n{'='*60}")
    print(f"  AI FAAN DISTRIBUTION")
    print(f"{'='*60}")
    for r in results:
        dist = r.get("aiFaanDist", {})
        if not dist:
            print(f"  Tier {r['adaptationTier']}: no AI wins")
            continue
        total = sum(dist.values())
        print(f"  Tier {r['adaptationTier']}:")
        for faan in sorted(dist.keys(), key=int):
            count = dist[faan]
            pct = 100.0 * count / total if total > 0 else 0
            bar = "#" * int(pct / 2)
            print(f"    {faan:>2} faan: {count:>4} ({pct:>5.1f}%) {bar}")


if __name__ == "__main__":
    main()
