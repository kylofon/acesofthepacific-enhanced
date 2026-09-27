#pragma once
/* Program entry points (the `main` of each executable, called by prog_run after the c0 start-up).
 * Phase 4: placeholders (game/placeholder.c) that exercise the platform layer and the chain; phase 5
 * replaces them with the ported mains (INTRO 038e:0ac1, PS 048e:0259, AP 50c8:0008). */
#include "types.h"

int intro_main(int argc, u16 argv_ds);
int ps_main(int argc, u16 argv_ds);
int ap_main(int argc, u16 argv_ds);
