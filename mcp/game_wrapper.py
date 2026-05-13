"""ctypes wrapper around libmahjong-shared.dylib"""

import ctypes
import json
import os
import sys

# Locate the shared library
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.dirname(_SCRIPT_DIR)
_DEFAULT_DYLIB = os.path.join(_PROJECT_ROOT, "build", "core", "libmahjong-shared.dylib")
_DYLIB_PATH = os.environ.get("MAHJONG_DYLIB_PATH", _DEFAULT_DYLIB)

_lib = ctypes.cdll.LoadLibrary(_DYLIB_PATH)

# --- C ABI bindings ---

# Lifecycle
_lib.mahjong_create.argtypes = []
_lib.mahjong_create.restype = ctypes.c_void_p

_lib.mahjong_destroy.argtypes = [ctypes.c_void_p]
_lib.mahjong_destroy.restype = None

_lib.mahjong_start.argtypes = [ctypes.c_void_p]
_lib.mahjong_start.restype = None

_lib.mahjong_update.argtypes = [ctypes.c_void_p, ctypes.c_float]
_lib.mahjong_update.restype = None

# Human actions
_lib.mahjong_discard_tile.argtypes = [ctypes.c_void_p, ctypes.c_uint8]
_lib.mahjong_discard_tile.restype = None

_lib.mahjong_claim_by_index.argtypes = [ctypes.c_void_p, ctypes.c_int]
_lib.mahjong_claim_by_index.restype = None

_lib.mahjong_self_draw_win.argtypes = [ctypes.c_void_p]
_lib.mahjong_self_draw_win.restype = None

_lib.mahjong_self_kong.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint8]
_lib.mahjong_self_kong.restype = None

_lib.mahjong_advance_round.argtypes = [ctypes.c_void_p]
_lib.mahjong_advance_round.restype = None

# State queries
_lib.mahjong_current_phase.argtypes = [ctypes.c_void_p]
_lib.mahjong_current_phase.restype = ctypes.c_int

_lib.mahjong_is_game_over.argtypes = [ctypes.c_void_p]
_lib.mahjong_is_game_over.restype = ctypes.c_int

_lib.mahjong_wall_remaining.argtypes = [ctypes.c_void_p]
_lib.mahjong_wall_remaining.restype = ctypes.c_int

_lib.mahjong_active_player.argtypes = [ctypes.c_void_p]
_lib.mahjong_active_player.restype = ctypes.c_int

_lib.mahjong_can_self_draw_win.argtypes = [ctypes.c_void_p]
_lib.mahjong_can_self_draw_win.restype = ctypes.c_int

_lib.mahjong_human_claim_count.argtypes = [ctypes.c_void_p]
_lib.mahjong_human_claim_count.restype = ctypes.c_int

# Snapshot
_lib.mahjong_snapshot_json.argtypes = [ctypes.c_void_p]
_lib.mahjong_snapshot_json.restype = ctypes.c_void_p  # use c_void_p for safe free

_lib.mahjong_free_snapshot.argtypes = [ctypes.c_void_p]
_lib.mahjong_free_snapshot.restype = None

# Neural AI
_lib.mahjong_init_neural_ai.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_lib.mahjong_init_neural_ai.restype = None

_lib.mahjong_training_stats_json.argtypes = [ctypes.c_void_p]
_lib.mahjong_training_stats_json.restype = ctypes.c_void_p

_lib.mahjong_free_training_stats.argtypes = [ctypes.c_void_p]
_lib.mahjong_free_training_stats.restype = None

_lib.mahjong_run_self_play.argtypes = [ctypes.c_void_p, ctypes.c_int]
_lib.mahjong_run_self_play.restype = None

_lib.mahjong_set_verbose.argtypes = [ctypes.c_int]
_lib.mahjong_set_verbose.restype = None

# Benchmark
_lib.mahjong_run_benchmark.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
_lib.mahjong_run_benchmark.restype = ctypes.c_void_p

_lib.mahjong_free_benchmark_result.argtypes = [ctypes.c_void_p]
_lib.mahjong_free_benchmark_result.restype = None


# Phase enum (mirrors GamePhase C++ enum)
PHASE_DEALING = 0
PHASE_REPLACING_FLOWERS = 1
PHASE_PLAYER_DRAW = 2
PHASE_PLAYER_TURN = 3
PHASE_DISCARD_ANIMATION = 4
PHASE_CLAIM_PHASE = 5
PHASE_CLAIM_RESOLUTION = 6
PHASE_MELD_FORMATION = 7
PHASE_REPLACEMENT_DRAW = 8
PHASE_SCORING = 9
PHASE_ROUND_END = 10
PHASE_GAME_OVER = 11

PHASE_NAMES = {
    0: "DEALING", 1: "REPLACING_FLOWERS", 2: "PLAYER_DRAW",
    3: "PLAYER_TURN", 4: "DISCARD_ANIMATION", 5: "CLAIM_PHASE",
    6: "CLAIM_RESOLUTION", 7: "MELD_FORMATION", 8: "REPLACEMENT_DRAW",
    9: "SCORING", 10: "ROUND_END", 11: "GAME_OVER",
}

WIND_NAMES = {0: "East", 1: "South", 2: "West", 3: "North"}


class MahjongGame:
    """High-level wrapper around the C game engine."""

    def __init__(self):
        self._handle = _lib.mahjong_create()

    def __del__(self):
        if hasattr(self, "_handle") and self._handle:
            _lib.mahjong_destroy(self._handle)
            self._handle = None

    def start(self):
        _lib.mahjong_start(self._handle)

    def update(self, dt: float = 0.02):
        _lib.mahjong_update(self._handle, dt)

    def discard_tile(self, tile_id: int):
        _lib.mahjong_discard_tile(self._handle, tile_id)

    def claim_by_index(self, index: int):
        _lib.mahjong_claim_by_index(self._handle, index)

    def self_draw_win(self):
        _lib.mahjong_self_draw_win(self._handle)

    def self_kong(self, suit: int, rank: int):
        _lib.mahjong_self_kong(self._handle, suit, rank)

    def advance_round(self):
        _lib.mahjong_advance_round(self._handle)

    def current_phase(self) -> int:
        return _lib.mahjong_current_phase(self._handle)

    def is_game_over(self) -> bool:
        return _lib.mahjong_is_game_over(self._handle) != 0

    def active_player(self) -> int:
        return _lib.mahjong_active_player(self._handle)

    def can_self_draw_win(self) -> bool:
        return _lib.mahjong_can_self_draw_win(self._handle) != 0

    def human_claim_count(self) -> int:
        return _lib.mahjong_human_claim_count(self._handle)

    def init_neural_ai(self, model_dir: str):
        """Initialize neural network AI with trained models from model_dir."""
        _lib.mahjong_init_neural_ai(self._handle, model_dir.encode("utf-8"))

    @staticmethod
    def set_verbose(enabled: bool):
        """Enable or disable debug logging from the game engine."""
        _lib.mahjong_set_verbose(1 if enabled else 0)

    def run_self_play(self, num_games: int):
        """Run N self-play games (all AI, no rendering). Trains and saves models."""
        _lib.mahjong_run_self_play(self._handle, num_games)

    def training_stats(self) -> dict:
        """Get training stats as parsed JSON dict. Memory-safe."""
        ptr = _lib.mahjong_training_stats_json(self._handle)
        try:
            raw = ctypes.cast(ptr, ctypes.c_char_p).value
            return json.loads(raw)
        finally:
            _lib.mahjong_free_training_stats(ptr)

    def snapshot_json(self) -> dict:
        """Get game snapshot as parsed JSON dict. Memory-safe."""
        ptr = _lib.mahjong_snapshot_json(self._handle)
        try:
            raw = ctypes.cast(ptr, ctypes.c_char_p).value
            return json.loads(raw)
        finally:
            _lib.mahjong_free_snapshot(ptr)

    def advance_until_human_input(self, max_iters: int = 50000) -> dict:
        """Tick the game forward until it needs human input or reaches a terminal state.
        Returns the snapshot at the stopping point."""
        for _ in range(max_iters):
            phase = self.current_phase()

            # Human's turn to discard/kong/win
            if phase == PHASE_PLAYER_TURN and self.active_player() == 0:
                # One more update so handlePlayerTurn() registers the
                # discard callback on HumanPlayer before we return.
                self.update(0.02)
                break

            # Human has claim options
            if phase == PHASE_CLAIM_PHASE and self.human_claim_count() > 0:
                # Run handleClaimPhase at least once so AI decisions are
                # collected (first entry) or a prior human response is
                # processed (re-entry after claim_by_index).
                self.update(0.02)
                # If the claim was already resolved, phase will have changed
                if self.current_phase() != PHASE_CLAIM_PHASE:
                    continue
                break

            # Terminal / display states
            if phase in (PHASE_SCORING, PHASE_ROUND_END, PHASE_GAME_OVER):
                break

            self.update(0.02)

        return self.snapshot_json()

    @staticmethod
    def run_benchmark(num_games: int, adaptation_tier: int = 0, weights_dir: str = "") -> dict:
        """Run benchmark: num_games of all-heuristic self-play with given adaptation tier.
        Returns JSON dict with win rates."""
        ptr = _lib.mahjong_run_benchmark(
            num_games, adaptation_tier,
            weights_dir.encode("utf-8") if weights_dir else None
        )
        try:
            raw = ctypes.cast(ptr, ctypes.c_char_p).value
            return json.loads(raw)
        finally:
            _lib.mahjong_free_benchmark_result(ptr)
