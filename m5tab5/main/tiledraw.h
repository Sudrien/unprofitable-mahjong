/*
 * tiledraw.h -- a tile on gfx's shadow buffer: its face, the two-tone
 * edge under it that makes it read as a block, and the outline that marks
 * it selected or hinted.
 *
 * The face is drawn from tilefaces' run-length rows straight into
 * gfx_fb(), clipped to the screen; nothing is unpacked first.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "tilefaces.h"

/* How far the edge shows below the face, per size. */
#define TILEDRAW_EDGE_L   (6)
#define TILEDRAW_EDGE_M   (4)
#define TILEDRAW_EDGE_S   (2)

/* The face of tile `kind` with its top-left at (x, y), every opaque pixel
 * in `tint` instead of its own colour when tint is nonzero (the edge). */
void tiledraw_face(const tilefaces_size_t *sz, int kind, int x, int y, uint16_t tint);

/* A whole tile: edge, then face. `edge` is TILEDRAW_EDGE_*. */
void tiledraw_tile(const tilefaces_size_t *sz, int kind, int x, int y, int edge);

/* A frame `t` pixels thick just outside the tile's face and edge. */
void tiledraw_ring(const tilefaces_size_t *sz, int x, int y, int edge, int t, uint16_t c);
