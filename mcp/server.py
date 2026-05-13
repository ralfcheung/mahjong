"""MCP server for playing Hong Kong Mahjong via Claude."""

import logging
import os
import sys

from mcp.server.fastmcp import FastMCP
from game_wrapper import (
    MahjongGame,
    PHASE_PLAYER_TURN,
    PHASE_CLAIM_PHASE,
    PHASE_SCORING,
    PHASE_ROUND_END,
    PHASE_GAME_OVER,
    PHASE_NAMES,
    WIND_NAMES,
)

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.dirname(_SCRIPT_DIR)
_MODEL_DIR = os.path.join(_PROJECT_ROOT, "assets", "model")

logging.basicConfig(
    stream=sys.stderr,
    level=logging.DEBUG,
    format="[mahjong-mcp] %(message)s",
)
log = logging.getLogger("mahjong-mcp")

mcp = FastMCP("mahjong")

# Single game instance
_game: MahjongGame | None = None


def _ensure_game() -> MahjongGame:
    global _game
    if _game is None:
        raise RuntimeError("No game in progress. Call new_game first.")
    return _game


def format_game_state(snap: dict) -> str:
    """Render a snapshot into Claude-friendly text."""
    lines = []
    phase = snap["phase"]
    phase_name = PHASE_NAMES.get(phase, f"UNKNOWN({phase})")
    active = snap["activePlayerIndex"]
    wind = WIND_NAMES.get(snap["prevailingWind"], "?")

    lines.append(f"=== Mahjong — {phase_name} ===")
    lines.append(f"Prevailing wind: {wind} | Wall remaining: {snap['wallRemaining']}")
    lines.append("")

    # Action prompt
    if phase == PHASE_PLAYER_TURN and active == 0:
        lines.append(">> YOUR TURN: Discard a tile (use tile id).")
        if snap.get("canSelfDrawWin"):
            lines.append("   You can also declare SELF-DRAW WIN!")
        if snap.get("canSelfKong") and snap.get("selfKongOptions"):
            lines.append("   You can also declare SELF KONG.")
    elif phase == PHASE_CLAIM_PHASE and snap.get("humanClaimOptions"):
        lines.append(">> CLAIM PHASE: Choose a claim option or pass (-1).")
    elif phase == PHASE_SCORING:
        lines.append(">> SCORING: Review results, then call advance_round.")
    elif phase == PHASE_ROUND_END:
        lines.append(">> ROUND END: Call advance_round to continue.")
    elif phase == PHASE_GAME_OVER:
        lines.append(">> GAME OVER.")
    else:
        lines.append(f"   (Waiting... active player: {active})")
    lines.append("")

    # Your hand (player 0)
    me = snap["players"][0]
    lines.append(f"--- Your Hand ({me['name']}, {WIND_NAMES.get(me['seatWind'], '?')} wind, score: {me['score']}) ---")
    if me["concealed"]:
        grouped = _group_tiles_by_suit(me["concealed"])
        for suit_name, tiles in grouped:
            tile_strs = [f"{t['name']} [id:{t['id']}]" for t in tiles]
            lines.append(f"  {suit_name}: {', '.join(tile_strs)}")
    if me["melds"]:
        meld_strs = [_format_meld(m) for m in me["melds"]]
        lines.append(f"  Melds: {' | '.join(meld_strs)}")
    if me["flowers"]:
        lines.append(f"  Flowers: {', '.join(t['name'] for t in me['flowers'])}")
    lines.append("")

    # Claim options
    if phase == PHASE_CLAIM_PHASE and snap.get("humanClaimOptions"):
        lines.append("Available claims:")
        for opt in snap["humanClaimOptions"]:
            tiles_str = ", ".join(t["name"] for t in opt["meldTiles"]) if opt["meldTiles"] else ""
            extra = f" (with {tiles_str})" if tiles_str else ""
            lines.append(f"  [{opt['index']}] {opt['type'].upper()}{extra}")
        lines.append(f"  [-1] PASS")
        lines.append("")

    # Self kong options
    if phase == PHASE_PLAYER_TURN and snap.get("selfKongOptions"):
        lines.append("Self kong options:")
        for opt in snap["selfKongOptions"]:
            kind = "promote" if opt["isPromote"] else "concealed"
            lines.append(f"  suit={opt['suit']} rank={opt['rank']} ({kind})")
        lines.append("")

    # Last discard
    if snap.get("hasLastDiscard") and snap.get("lastDiscard"):
        ld = snap["lastDiscard"]
        lines.append(f"Last discard: {ld['name']} (by player {active})")
        lines.append("")

    # Other players
    for i in range(1, 4):
        p = snap["players"][i]
        lines.append(f"--- Player {i}: {p['name']} ({WIND_NAMES.get(p['seatWind'], '?')}, score: {p['score']}) ---")
        lines.append(f"  Concealed: {p['concealedCount']} tiles")
        if p["melds"]:
            meld_strs = [_format_meld(m) for m in p["melds"]]
            lines.append(f"  Melds: {' | '.join(meld_strs)}")
        if p["flowers"]:
            lines.append(f"  Flowers: {', '.join(t['name'] for t in p['flowers'])}")
        if p["discards"]:
            lines.append(f"  Discards: {', '.join(t['name'] for t in p['discards'])}")
    lines.append("")

    # Scoring breakdown
    scoring = snap.get("scoring", {})
    if phase in (PHASE_SCORING, PHASE_ROUND_END, PHASE_GAME_OVER) and scoring:
        if scoring.get("isDraw"):
            lines.append("Result: DRAW (wall exhausted)")
        elif scoring.get("winnerIndex", -1) >= 0:
            winner = scoring["winnerIndex"]
            wname = snap["players"][winner]["name"]
            method = "Self-draw" if scoring.get("selfDrawn") else "Discard"
            lines.append(f"Winner: {wname} (Player {winner}) by {method}")
            lines.append(f"Total faan: {scoring['totalFaan']}" +
                         (" (LIMIT)" if scoring.get("isLimit") else ""))
            lines.append(f"Base points: {scoring['basePoints']}")
            if scoring.get("breakdown"):
                lines.append("Breakdown:")
                for entry in scoring["breakdown"]:
                    lines.append(f"  {entry['nameEn']} — {entry['faan']} faan")
        lines.append("")

    # Training stats (when neural AI is active)
    training = snap.get("training")
    if training:
        lines.append("--- Neural AI Training ---")
        lines.append(f"  Epsilon: {training['epsilon']:.4f}")
        lines.append(f"  Games played: {training['gamesPlayed']}")
        lines.append(f"  Avg loss: {training['avgLoss']:.6f}")
        lines.append(f"  Total transitions: {training['totalTransitions']}")
        lines.append("")

    return "\n".join(lines)


def _group_tiles_by_suit(tiles: list) -> list:
    """Group tiles by suit name for display."""
    suit_names = {0: "Bamboo", 1: "Characters", 2: "Dots", 3: "Wind", 4: "Dragon", 5: "Flower", 6: "Season"}
    groups = {}
    for t in tiles:
        sn = suit_names.get(t["suit"], f"Suit{t['suit']}")
        groups.setdefault(sn, []).append(t)
    return sorted(groups.items())


def _format_meld(m: dict) -> str:
    """Format a meld for display."""
    meld_types = {0: "Chow", 1: "Pung", 2: "Kong", 3: "Pair", 4: "Eyes"}
    mtype = meld_types.get(m["type"], f"Type{m['type']}")
    tiles_str = " ".join(t["name"] for t in m["tiles"])
    exposed = "exposed" if m.get("exposed") else "concealed"
    return f"{mtype}({tiles_str}, {exposed})"


# --- MCP Tools ---

@mcp.tool()
def new_game() -> str:
    """Start a new mahjong game. You are Player 0 (human) against 3 AI opponents.
    Returns the initial game state after dealing."""
    global _game
    log.info("new_game called")
    _game = MahjongGame()
    _game.start()
    _game.init_neural_ai(_MODEL_DIR)
    snap = _game.advance_until_human_input()
    log.info("new_game done — phase=%s wall=%s hand=%d tiles",
             PHASE_NAMES.get(snap["phase"]), snap["wallRemaining"], len(snap["players"][0]["concealed"]))
    return format_game_state(snap)


@mcp.tool()
def get_game_state() -> str:
    """Get the current game state without taking any action."""
    game = _ensure_game()
    snap = game.snapshot_json()
    return format_game_state(snap)


@mcp.tool()
def discard_tile(tile_id: int) -> str:
    """Discard a tile from your hand by its tile ID.
    Only valid during PLAYER_TURN when you are the active player (player 0).
    Use the [id:N] shown in your hand to pick which tile to discard."""
    game = _ensure_game()
    phase = game.current_phase()
    active = game.active_player()
    log.info("discard_tile called — tile_id=%r (type=%s) phase=%s active=%s",
             tile_id, type(tile_id).__name__, PHASE_NAMES.get(phase), active)

    if phase != PHASE_PLAYER_TURN or active != 0:
        msg = f"Cannot discard now. Phase: {PHASE_NAMES.get(phase, phase)}, active: {active}"
        log.warning("discard_tile rejected: %s", msg)
        return msg

    # Log valid tile IDs in hand before discard
    snap_before = game.snapshot_json()
    valid_ids = [t["id"] for t in snap_before["players"][0]["concealed"]]
    log.info("valid tile ids in hand: %s", valid_ids)
    log.info("tile_id %r in valid_ids: %s", tile_id, tile_id in valid_ids)

    wall_before = snap_before["wallRemaining"]
    game.discard_tile(tile_id)

    # Check if discard took effect by looking at phase change
    phase_after_discard = game.current_phase()
    log.info("phase after discard_tile call: %s", PHASE_NAMES.get(phase_after_discard))

    snap = game.advance_until_human_input()
    log.info("after advance — phase=%s wall=%s hand=%d tiles",
             PHASE_NAMES.get(snap["phase"]), snap["wallRemaining"],
             len(snap["players"][0]["concealed"]))

    if snap["wallRemaining"] == wall_before and snap["phase"] == PHASE_PLAYER_TURN:
        log.error("DISCARD HAD NO EFFECT — tile_id=%r was not found in hand or callback not set", tile_id)

    return format_game_state(snap)


@mcp.tool()
def claim(option_index: int) -> str:
    """Claim the last discard. Use the index shown in Available claims.
    Pass -1 to pass (skip claiming).
    Only valid during CLAIM_PHASE when you have claim options."""
    game = _ensure_game()
    phase = game.current_phase()
    if phase != PHASE_CLAIM_PHASE:
        return f"Cannot claim now. Phase: {PHASE_NAMES.get(phase, phase)}"
    game.claim_by_index(option_index)
    snap = game.advance_until_human_input()
    return format_game_state(snap)


@mcp.tool()
def declare_self_draw_win() -> str:
    """Declare a self-draw win (tsumo). Only valid during PLAYER_TURN
    when canSelfDrawWin is shown."""
    game = _ensure_game()
    phase = game.current_phase()
    if phase != PHASE_PLAYER_TURN or game.active_player() != 0:
        return f"Cannot declare win now. Phase: {PHASE_NAMES.get(phase, phase)}"
    if not game.can_self_draw_win():
        return "You cannot self-draw win with your current hand."
    game.self_draw_win()
    snap = game.advance_until_human_input()
    return format_game_state(snap)


@mcp.tool()
def declare_self_kong(suit: int, rank: int) -> str:
    """Declare a self kong (concealed or promoted). Provide the suit and rank
    of the tile to kong. Only valid during PLAYER_TURN when self kong options exist."""
    game = _ensure_game()
    phase = game.current_phase()
    if phase != PHASE_PLAYER_TURN or game.active_player() != 0:
        return f"Cannot declare kong now. Phase: {PHASE_NAMES.get(phase, phase)}"
    game.self_kong(suit, rank)
    snap = game.advance_until_human_input()
    return format_game_state(snap)


@mcp.tool()
def run_self_play(num_games: int = 100) -> str:
    """Run N self-play training games (pure C++ AI, no rendering).
    Much faster than playing through MCP — hundreds of games per second.
    Trains the neural networks and saves updated models automatically."""
    game = _ensure_game()
    log.info("run_self_play called — num_games=%d", num_games)
    game.run_self_play(num_games)
    stats = game.training_stats()
    log.info("self-play done — stats=%s", stats)
    return (
        f"Completed {num_games} self-play games.\n"
        f"Games played total: {stats.get('gamesPlayed', '?')}\n"
        f"Epsilon: {stats.get('epsilon', '?'):.4f}\n"
        f"Avg loss: {stats.get('avgLoss', '?'):.6f}\n"
        f"Total transitions: {stats.get('totalTransitions', '?')}\n"
    )


@mcp.tool()
def advance_round() -> str:
    """Advance to the next round after scoring/round end. Also works at game over."""
    game = _ensure_game()
    phase = game.current_phase()
    if phase not in (PHASE_SCORING, PHASE_ROUND_END, PHASE_GAME_OVER):
        return f"Cannot advance now. Phase: {PHASE_NAMES.get(phase, phase)}"
    game.advance_round()
    snap = game.advance_until_human_input()
    return format_game_state(snap)


if __name__ == "__main__":
    mcp.run()
