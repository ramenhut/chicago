
#ifndef __EVX_INTRA_PRED_H__
#define __EVX_INTRA_PRED_H__

#include "base.h"
#include "macroblock.h"
#include "imageset.h"

namespace evx {

#define EVX_INTRA_MODE_DC         0
#define EVX_INTRA_MODE_VERT       1
#define EVX_INTRA_MODE_HORIZ      2
#define EVX_INTRA_MODE_DIAG_DL    3
#define EVX_INTRA_MODE_DIAG_DR    4
#define EVX_INTRA_MODE_VERT_R     5
#define EVX_INTRA_MODE_HORIZ_D    6
#define EVX_INTRA_MODE_NONE       7
#define EVX_INTRA_MODE_COUNT      7  // modes 0-6 are directional

// Generate spatial prediction for Y plane (16x16) and U/V planes (8x8).
// Reads neighbors from recon (prediction_cache). Uses 128 at frame edges.
void generate_intra_prediction(uint8 mode, const image_set &recon,
                                int32 px, int32 py, macroblock *pred);

// Try all 7 directional modes, return the one with minimum SAD against source.
// Optionally returns the SAD of the best mode via out_sad.
uint8 select_best_intra_mode(const macroblock &source, const image_set &recon,
                              int32 px, int32 py, macroblock *scratch, int32 *out_sad);

// Returns 3 gradient-pruned candidate modes (always returns 3).
uint8 select_intra_mode_candidates(const image_set &recon, int32 px, int32 py, uint8 *candidates);

} // namespace evx

#endif // __EVX_INTRA_PRED_H__
