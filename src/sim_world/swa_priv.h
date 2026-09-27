#pragma once
/* sim_world part A (flight model, weapons, damage, deaths, impacts, objects, ships, snario, acedata):
 * private helpers shared by the part-A files. Game state stays in mem[] (PORTING.md); these are only
 * accessors and the arithmetic idioms the original compiler emitted everywhere. */
#include <string.h>
#include "sim_world/sim_world.h"
#include "symbols_ap.h"
#include "render3d/render3d.h"
#include "sim_flow/sim_flow.h"
#include "sound/sound.h"
#include "platform/platform.h"
#include "video/video.h"
#include "prog.h"
#include "codeptr.h"

/* abs of a 16-bit value as the code does it: 8000h -> 7FFFh (`cmp ax,8000h / cwd / xor / sub`) */
static inline s16 abs16(s16 x) { return x == (s16)0x8000 ? 0x7FFF : (s16)(x < 0 ? -x : x); }
/* distance of a roll angle from level: abs16, folded above 90 degrees */
static inline s16 fold16(s16 r) { s16 a = abs16(r); return a > 0x4000 ? (s16)(0x7FFF - a) : a; }
/* (i32)a * b >> n truncated to 16 bits (movsx/imul/sar) */
static inline s16 mulsh(s16 a, s16 b, int n) { return (s16)(((s32)a * (s32)b) >> n); }
static inline s16 smin(s16 a, s16 b) { return a < b ? a : b; }
static inline s16 smax(s16 a, s16 b) { return a > b ? a : b; }
static inline s32 labs32(s32 v) { return v < 0 ? -v : v; }

/* object field accessors (near objects in DGROUP) */
#define OB(o, off)  DSB((u16)((o) + (off)))
#define OC(o, off)  DSC((u16)((o) + (off)))
#define OW(o, off)  DSW((u16)((o) + (off)))
#define OS(o, off)  DSS((u16)((o) + (off)))
#define OL(o, off)  DSL((u16)((o) + (off)))
#define OSL(o, off) DSSL((u16)((o) + (off)))
/* far object accessors */
#define FB(p, o)  (*(u8  *)mp((p).seg, (u16)((p).off + (o))))
#define FC(p, o)  (*(s8  *)mp((p).seg, (u16)((p).off + (o))))
#define FW(p, o)  (*(u16 *)mp((p).seg, (u16)((p).off + (o))))
#define FS(p, o)  (*(s16 *)mp((p).seg, (u16)((p).off + (o))))
#define FL(p, o)  (*(u32 *)mp((p).seg, (u16)((p).off + (o))))
#define FSL(p, o) (*(s32 *)mp((p).seg, (u16)((p).off + (o))))

/* frequently used AP globals */
#define DT            DSS(0x711E)            /* frame time scale, 100h = nominal */
#define PLAYER        DSW(0x3B09)            /* player Plane (near) */
#define RNG_ST        0x4454                 /* sim_flow's RNG state */

/* objects.c file-local helpers used before their definition */
void obj_set_class(u16 o, u16 k);

/* acedata.c / snario.c file-level functions without callers in the port yet */
FarPtr file_load_far(u16 name_ds);
void misctbl_load(u16 area_ds);
void misctbl_free(void);

/* fm_core.c internal helpers exported to the other part-A files */
s16  fm_drag(s16 v, s16 k, u16 frac_ds);
s16  ap_clamp_sym_0b01(s16 x, s16 m);
s16  step_toward(s16 a, s16 b, s16 m);
s16  alt_band(s32 z);
void fm_set_cruise(u16 fm, u16 fmd, s16 k, s32 alt);
void fm_full(u16 fm, u16 ctl);
void fm_simple(u16 fm, u16 ctl, s16 level_hold, s16 coord, s16 p5);
void fm_ground_contact(u16 F, u16 fm, u16 ctl, u16 P);
void fm_integrate_vel(u16 v, u16 frac, u16 f);
void pos_step(u16 pos, u16 frac, s16 v);
void fm_per_frame_velocity(u16 fm);
void copy_vec4(void);

/* weapons.c file-level functions */
u16  weapon_class_register(FarPtr update, FarPtr draw);
u8   bullet_spawn(s16 side, s16 dx, s16 dz, u16 pos, u16 ang, u16 src);
void bullet_update(FarPtr b);
void bomb_update(FarPtr b);

/* fm_step.c */
u16  fly_player_is_ai(void);
void steer_heading(u16 p, u16 tgt, s16 dhead, s16 lead);

/* developer test (swa_test.c) */
int  sim_world_a_dev_test(void);
