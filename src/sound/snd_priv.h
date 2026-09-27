#pragma once
/* Sound module internals (not part of the module interface): the AdLib driver model and the glue
 * between the player, the API and the developer test. */
#include "sound/sound.h"

/* ---- The SCI driver calling convention (sound.md §8.1): lcall driver:0000 with BP = function number,
 * AX / CX (ES:AX for fn 1) in and out. Flags are restored by the driver. */
typedef struct {
    u16 ax, cx, es;
} DrvRegs;

/* The AdLib driver SSM:ADL: (sound.md §8), modelled in C. drv = the far address the player stores in
 * cs:0371 (the driver block loaded from the OVL, offset 0): the driver keeps all its state in that
 * block at its original offsets, as the real driver does (position independent, cs-relative data).
 * PORT: every driver number runs this model (sound.md §13, PLAN.md decision 1). */
void adl_driver(FarPtr drv, u16 bp, DrvRegs *r);

/* OPL2 register write used by the driver model: host_opl_write by default; the DRO test captures it. */
extern void (*snd_opl_write)(u8 reg, u8 val);

/* Player placement. The player's variables live in its own code segment (sound.md §3.4): INTRO 1373,
 * PS 430a (offsets +1), AP 3060 (offsets +4). player_attach picks the current program's segment; with
 * no program running (the developer test) the base set here is used. Host-side cache only. */
void player_set_test_base(u16 seg, u16 delta);
/* Developer test: is the node in the player's list? */
bool player_test_listed(FarPtr n);
/* Developer aid: incremented each time a sound restarts at its loop point (p_sequence end of sound). */
extern u32 snd_dbg_restarts;
/* Detach the sound service from the audio thread (program switch / shutdown). */
void snd_host_detach(void);
/* Test entry (sound/snd_test.c): AOTP_TEST=dro. Returns the process exit code. */
int snd_test_dro(void);
