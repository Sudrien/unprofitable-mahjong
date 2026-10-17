/*
 * ui.h -- the game's one screen: scores along the top, the rail of
 * recent clears, the melds, the hand, a status line and the buttons.
 * Landscape, 1280x720 logical, drawn into gfx's shadow buffer.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

/* After the panel, gfx (rotated to landscape), touch and NVS are up.
 * Loads the saved game or deals a new one, then never returns. */
void ui_run(void);
