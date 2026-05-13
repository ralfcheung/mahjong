#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque game handle
typedef void* MahjongGame;

// Logging
void mahjong_set_verbose(int enabled);

// Lifecycle
MahjongGame mahjong_create(void);
void mahjong_destroy(MahjongGame game);
void mahjong_start(MahjongGame game);
void mahjong_update(MahjongGame game, float dt);

// Human actions
void mahjong_discard_tile(MahjongGame game, uint8_t tileId);
void mahjong_claim_pass(MahjongGame game);
void mahjong_claim_pung(MahjongGame game);
void mahjong_claim_kong(MahjongGame game);
void mahjong_claim_chow(MahjongGame game, uint8_t handRank1, uint8_t handRank2);
void mahjong_claim_win(MahjongGame game);
void mahjong_self_draw_win(MahjongGame game);
void mahjong_self_kong(MahjongGame game, uint8_t suit, uint8_t rank);
void mahjong_claim_by_index(MahjongGame game, int index);  // -1 = pass
void mahjong_advance_round(MahjongGame game);

// State queries
int mahjong_current_phase(MahjongGame game);
int mahjong_is_game_over(MahjongGame game);
int mahjong_wall_remaining(MahjongGame game);
int mahjong_active_player(MahjongGame game);
int mahjong_can_self_draw_win(MahjongGame game);
int mahjong_human_claim_count(MahjongGame game);

// Snapshot - caller must free returned buffer with mahjong_free_snapshot
// Returns a JSON-encoded GameSnapshot string
char* mahjong_snapshot_json(MahjongGame game);
void mahjong_free_snapshot(char* json);

// Neural AI (requires HAS_TORCH build)
void mahjong_init_neural_ai(MahjongGame game, const char* modelDir);
void mahjong_run_self_play(MahjongGame game, int numGames);
char* mahjong_training_stats_json(MahjongGame game);
void mahjong_free_training_stats(char* json);

// Benchmark: run numGames of all-heuristic self-play with given adaptation tier.
// weightsDir: path to directory containing discard_weights.bin/claim_weights.bin
//             (needed for adaptation tiers > 0; pass "" or NULL for tier 0)
// Returns JSON string with results. Caller must free with mahjong_free_benchmark_result.
char* mahjong_run_benchmark(int numGames, int adaptationTier, const char* weightsDir);
void mahjong_free_benchmark_result(char* json);

#ifdef __cplusplus
}
#endif
