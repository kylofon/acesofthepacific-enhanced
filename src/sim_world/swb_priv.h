#pragma once
/* sim_world part B (the AI: ai_exec, ai_target, ai_combat, ai_dogfight, ai_manoeuvre, ai_tactics,
 * ai_elements, ai_flight, ai_waypoints, ai_orders, g_attack): private helpers shared by the part-B files.
 * Game state stays in mem[] (PORTING.md); these are accessors and idioms only. Uses part A's accessors
 * (OB/OW/OS/OL/OSL, abs16, DT, PLAYER) from swa_priv.h. */
#include <stdarg.h>
#include "sim_world/swa_priv.h"
#include "codeptr.h"

/* ---- state descriptors (DS addresses act as state ids, sim_world_ai.md §1.1) ------------------------ */
#define D_PATH          0x03B8
#define D_CV_TAKEOFF    0x03CC
#define D_RW_TAKEOFF    0x03E0
#define D_PARK_CV       0x03F4
#define D_PARK_FIELD    0x0408
#define D_WAYPOINTS     0x041C
#define D_JOIN          0x0430
#define D_KEEP_FORM     0x0444
#define D_CLIMB_OUT     0x0458
#define D_ORBIT_HOME    0x046C
#define D_ORBIT_WP      0x0480
#define D_RETURN        0x0494
#define D_ESCORT_JOIN   0x04A8
#define D_ESCORT_FLY    0x04BC
#define D_GOTO_TARGET   0x04D0
#define D_SHIP_STOP     0x0682
#define D_PAIR_PURSUE   0x12DE
#define D_SPREAD        0x12F2
#define D_BRACKET       0x1306
#define D_PAIR_ATTACK   0x131A
#define D_PAIR_BREAK    0x132E
#define D_DECK_WRECK    0x1384
#define D_DIVEBOMB      0x14A5
#define D_TORPEDO       0x14B9
#define D_GLIDE         0x14CD
#define D_LOW           0x14E1
#define D_BOMBER        0x14F5
#define D_ROCKET        0x1509
#define D_STRAFE        0x151D
#define D_TAC_A         0x1560
#define D_TAC_B         0x1574
#define D_TAC_C         0x1588
#define D_TAC_D         0x159C
#define D_TAC_E         0x15B0
#define D_HOLD          0x15F6
#define D_CLIMB         0x16B5
#define D_PURSUE        0x16C9
#define D_DIVE          0x16DD
#define D_GUNS          0x16F1
#define D_DIVE2         0x1705
#define D_HEADON_A      0x1719
#define D_HEADON_B      0x172D
#define D_MANOEUVRE     0x1741
#define D_MNV_EXTEND    0x1755
#define D_DEFEND        0x1769
#define D_DOGFIGHT      0x177D
#define D_LEVEL         0x1791
#define D_EXTEND        0x17A5
#define D_RECOVER       0x17B9
#define D_HEADON        0x17CD
#define D_LEVEL2        0x17E1
#define D_REGROUP       0x17F5
#define D_COVER         0x1809
#define D_REJOIN        0x181D
#define D_CROSS         0x1831
#define D_WEAVE         0x1845
#define D_BANK          0x1859
#define D_PULLUP        0x186D
#define D_SHIP_ATTACK   0x5800      /* pseudo descriptors of the target chooser (§5) */
#define D_PLACE_ATTACK  0x57EC

/* ---- common object access ------------------------------------------------------------------------- */
#define DESC(p)     OW(p, 0x4A)                          /* current state descriptor */
#define SCR(p)      OW(p, 0x54)                          /* brain scratch block */
#define FLT(p)      OW(p, 0x34)                          /* flight record */
#define DFLAGS(d)   DSW((u16)((d) + 6))                  /* descriptor flags */
/* aircraft .DAT of a plane: DS:55D2[DS:54BC[type]] */
static inline u16 DAT(u16 p) { return DSW((u16)(DSW(0x55D2) + 2 * DSB((u16)(0x54BC + OB(p, 0x19))))); }
/* ship .DAT of a ship: DS:55D0[DS:555B[type]] */
static inline u16 SDAT(u16 s) { return DSW((u16)(DSW(0x55D0) + 2 * DSB((u16)(0x555B + OB(s, 0x19))))); }
/* pilot profile: DS:55CE[+BB] */
static inline u16 PIL(u16 p) { return DSW((u16)(DSW(0x55CE) + 2 * OB(p, 0xBB))); }
#define PW(p, off)  DSS((u16)(PIL(p) + (off)))           /* pilot profile word (signed) */
#define CLS(p)      ((s16)OC(DAT(p), 0x38))              /* aircraft class: 0 fighter, 2 dive bomber, 3 bomber */

/* ---- the RNG (sim_flow 1889, state DS:4454) — every call is part of the VCR contract (§18) ---------- */
u16 swb_rng(void);                                       /* 1889:005f with DS:4454 (traced by the dev test) */
s16 swb_rng_range(s16 n);                                /* 1889:00b4 */
#define RNG()        swb_rng()
#define RNG_RANGE(n) swb_rng_range(n)

/* ---- math wrappers ------------------------------------------------------------------------------------ */
static inline s32 dist2(u16 a, u16 b) { return (s32)ldist2((u16)(a + 4), (u16)(b + 4)); }   /* objects */
static inline s32 dist2v(u16 a, u16 b) { return (s32)ldist2(a, b); }                          /* vectors */
static inline s32 dist3(u16 a, u16 b) { return (s32)ldist3((u16)(a + 4), (u16)(b + 4)); }
static inline s32 dist3v(u16 a, u16 b) { return (s32)ldist3(a, b); }
#define TIMER16(p)   timer16_dec(p)
#define THROTTLE(p)  OW(p, 0xB2)
#define CRUISE(p)    plane_cruise_throttle(p)

/* brain calls */
static inline u16 set_state(u16 p, u16 desc) { return brain_set_state((u16)(p + 0x4A), desc); }
u16 swb_call_desc(u16 desc, u16 slot, u16 obj);          /* far fn at desc+slot (8 steer, 0C act, 10 think) */

/* radio record for 1815:06e7 / 06d0 (the original passes a partly uninitialised stack record;
 * PORT: zeroed) */
static inline u16 radio_rec(u16 size) { u16 r = stk_alloc(size); memset(DSP(r), 0, size); return r; }

/* ---- functions of this part that the header does not export ------------------------------------------ */
/* ai_exec.c */
u16  brain_jitter(void);
void ai_call_help(u16 victim, u16 f);
u16  brain_threat_check(u16 p);
void brain_think(u16 obj, u16 brain);
void brain_act(u16 obj, u16 brain);
void alert_tick(u16 p);
void alert_clear(u16 p);
/* ai_target.c */
u16  ap_target_class(u16 p, u16 t, u16 is_place);
u16  choose_target(u16 p, u16 bomber_type, u16 ga);
void engage_best(u16 p);
u16  bomber_cover_think(u16 p);
u16  low_power(u16 p);
u16  flight_protects_obj(u16 obj, u16 p);
u16  covers(u16 t, u16 p);
u16  flight_all_done(u16 f);
/* ai_combat.c */
u16  aim_solution(u16 p, u16 t);
u16  nearest_enemy(u16 p, u16 d_ds);
/* ai_elements.c */
u16  can_see(u16 p, u16 t);
u16  my_wingman(u16 p);
/* ai_dogfight.c */
u16  st_climb_steer(u16 p);
u16  st_defend_steer(u16 p);
u16  weave(u16 p, u16 h0, u16 side);
void weave_enter(u16 p, u16 mate);
void extend_enter(u16 p, u16 unused);
u16  slot_offset(u16 p, u16 lead, u16 out);
u16  pursue_geometry(u16 p, u16 t);
void steer_lead_close(u16 p, u16 t, u16 ang);
u16  head_on_window(u16 p);
/* ai_tactics.c */
u16  tactic_choose(u16 p);
u16  tactic_energy(u16 p, u16 t);
u16  other_in_front(u16 p);
u16  energy_advantage(u16 p, u16 t);
void st_a_enter(u16 p, u16 t);
void st_c_enter(u16 p);
void st_hold_enter(u16 p, u16 next);
/* ai_flight.c */
u16  first_alive_of(u16 f);
u16  fly_to_point(u16 p, u16 pt, s16 tolh, s16 tolp, s16 dist);
u16  orbit_point(u16 p, u16 c, s32 r);
u16  find_ground_target(u16 p, s32 stop, u16 d_ds);
u16  flight_all_down(u16 f);
u16  near_home(u16 p);
/* g_attack.c */
u16  target_destroyed(u16 s);
u16  is_dive_bomber(u16 p);
u16  first_gun_type(u16 p);
u16  attack_reenter(u16 p);
u16  top_targets(u16 place, s16 n, u16 out_ds, s16 hard);
/* ai_waypoints.c */
u16  midpoint(u16 a, u16 b);
/* ai_orders.c */
u16  attacker_on_tail(u16 t);
u16  alive_count(u16 f);
