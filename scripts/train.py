#!/usr/bin/env python3
"""Run self-play training for the mahjong AI."""

import argparse
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "mcp"))
from game_wrapper import MahjongGame


def main():
    parser = argparse.ArgumentParser(description="Mahjong self-play training")
    parser.add_argument("-n", "--num-games", type=int, default=100,
                        help="Number of self-play games (default: 100)")
    parser.add_argument("-m", "--model-dir", type=str, default="assets/model",
                        help="Model directory (default: assets/model)")
    args = parser.parse_args()

    model_dir = os.path.join(os.path.dirname(__file__), "..", args.model_dir)
    model_dir = os.path.abspath(model_dir)
    os.makedirs(model_dir, exist_ok=True)

    print(f"Model dir: {model_dir}")
    print(f"Games: {args.num_games}")

    game = MahjongGame()
    game.init_neural_ai(model_dir)
    game.run_self_play(args.num_games)

    stats = game.training_stats()
    print(f"\nTraining stats: {stats}")


if __name__ == "__main__":
    main()
