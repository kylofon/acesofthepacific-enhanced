/* shell_ui: post-mission messages (PS segment 050c) — shell_ui_debrief.md §2. */
#include "shell_ui/sui_a_priv.h"

u16 q_next(u16 q);                       /* shell_data/queue.c (PS 2242:0067) */

#define RESULT_FLAGS 0x4229              /* SIM.OUT result bits: 1 killed, 2 captured, 4 hospital, 70h friendly fire */
#define SIM_MODE     0x4B87              /* SIM.IN mode: 1 career */

/* PS 050c:0007 msg_fame_level -> 0..2 */
static u8 msg_fame_level(void)
{
    s16 v = (s16)(DSS(0x47BF) + (s16)(DSS(0x47C1) + DSS(0x47C3)) / 2);
    if (v < 5) return 0;
    return v < 30 ? 1 : 2;
}

/* PS 050c:0035 medal_check -> medal awarded or -1 */
static s8 medal_check(void)
{
    u16 m0 = stk_mark();
    u16 req = stk_alloc(8);                           /* victories, air, ground, ships */
    s16 v = DSS(0x4792);
    DSB(PS_DS_medal_awarded) = 0xFF;
    medals_open();
    for (s16 m = 0; m < 20; m++) {
        medal_req((s8)m, req);
        if (DSB((u16)(m + 0x4795)) == 0 && medal_for_service((s8)m, (s8)DSB(0x4776))) {
            if (DSS(req) != -1 && DSS(req) <= v) { DSB(PS_DS_medal_awarded) = (u8)m; break; }
            if (DSS(req) == -1 && (DSB(RESULT_FLAGS) & 4) && (s8)medal_is_wound((s8)m)) {
                DSB(PS_DS_medal_awarded) = (u8)m;
                break;
            }
        }
    }
    if (DSC(PS_DS_medal_awarded) == -1) {
        for (s16 m = 0; m < 20; m++) {
            medal_req((s8)m, req);
            if (DSB((u16)(m + 0x4795)) == 0 && DSS(req) != -1 &&
                (DSS(req + 2) <= DSS(0x423D) || DSS(req + 4) <= DSS(0x4237) || DSS(req + 6) <= DSS(0x423B)) &&
                medal_for_service((s8)m, (s8)DSB(0x4776))) {
                DSB(PS_DS_medal_awarded) = (u8)m;
                break;
            }
        }
    }
    if (DSC(PS_DS_medal_awarded) != -1) DSB((u16)(DSC(PS_DS_medal_awarded) + 0x4795)) = 1;
    medals_close();
    stk_release(m0);
    return DSC(PS_DS_medal_awarded);
}

/* PS 050c:0147 promotion_check -> new rank or -1 (the last qualifying rank wins) */
static s8 promotion_check(void)
{
    DSB(PS_DS_promotion_rank) = 0xFF;
    ranks_load();
    s8 top = rank_last((s8)DSB(0x4776));
    for (s8 r = (s8)(DSC(0x4777) + 1); r <= top; r++) {
        if ((s8)rank_service(r) == DSC(0x4776) && (s16)rank_points(r) <= DSS(0x47BD))
            DSB(PS_DS_promotion_rank) = (u8)r;
    }
    ranks_free();
    if (DSC(PS_DS_promotion_rank) > -1) DSB(0x4777) = DSB(PS_DS_promotion_rank);
    return DSC(PS_DS_promotion_rank);
}

static void msg_put(u16 kind, s16 id) { q_append(DSW(PS_DS_msg_queue), (s16)note_new(kind, (u16)id)); }

/* PS 050c:01ba msg_next -> next message record, 0 at the end (the queue is built on the first call) */
u16 msg_next(void)
{
    if (DSW(PS_DS_msg_queue) == 0) {
        ps_strings_load_24e3(make_name(0x0504 /* "displays" */, -1, 0x050D /* "dat" */));
        DSW(PS_DS_msg_queue) = q_new();
        u8 f = DSB(RESULT_FLAGS);
        if (f & 1) {                                                   /* killed */
            s8 k = 0;
            if (DSB(SIM_MODE) == 1) {
                u8 lv = msg_fame_level();
                k = lv == 0 ? 1 : lv == 1 ? 2 : 3;
            }
            msg_put(1, k);
        } else {
            bool done = false;
            if (DSB(SIM_MODE) == 1) {
                if (f & 2) {                                           /* captured */
                    u8 lv = msg_fame_level();
                    msg_put(2, lv == 0 ? 4 : lv == 1 ? 5 : 6);
                    done = true;
                } else if (f & 4) {
                    msg_put(3, 7);                                     /* hospital */
                }
            }
            if (!done && (f & 0x70)) {                                 /* friendly fire */
                msg_put(4, (s16)(8 + (DSB(SIM_MODE) == 1 ? DSC(0x4794) : 0)));
                if (DSB(0x4794) == 3) done = true;
            }
            if (!done && DSB(SIM_MODE) == 1) {
                s8 m = medal_check();
                if (m >= 0) msg_put(5, (s16)(m + 12));
                if (promotion_check() >= 0) msg_put(6, 32);
            }
        }
        strings_free();
        q_rewind(DSW(PS_DS_msg_queue));
    }
    if (q_at_end(DSW(PS_DS_msg_queue))) {
        q_delete(DSW(PS_DS_msg_queue));
        DSW(PS_DS_msg_queue) = 0;
        return 0;
    }
    return q_next(DSW(PS_DS_msg_queue));
}
