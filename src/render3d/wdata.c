/* render3d — wdata.c (AP 628f): the TARGET.n file in memory (dated state lists per target).
 * render3d_world.md §6.5.
 *
 * Target node (14 bytes, arena_alloc): +00 next, +02 id, +04 u16 head[5]. Dated entry (8 bytes): +00
 * next, +02 date, +04 payload (list 0: near pointer to P·12 bytes of points; list 1: two words; lists
 * 2–4: one word). */
#include "render3d/r3d_priv.h"
#include "platform/platform.h"

enum {
    T_P = 0x0C1C,          /* points per position list */
    T_CUR = 0x0C1E,        /* current target */
    T_ENTRY = 0x0C20,      /* current entry */
    T_MARK = 0x0C22,       /* arena mark */
    T_INF = 0x0C24,        /* "INF:" */
    T_DAT = 0x0C29,        /* "DAT:" */
    T_LIST = 0x511E,       /* current list */
    T_FIRST = 0x5120,      /* first target */
};
#define WDATA_TAG far_make(SEG(0x3968), 0)

/* AP 628f:0000 — point i of the current entry */
void wd_list0(s16 i, u16 v) { memcpy(DSP((u16)(DSW(DSW(T_ENTRY) + 4) + 12 * i)), DSP(v), 12); }
/* AP 628f:0024 — the entry's payload dword */
void wd_list1(u16 v) { DSL(DSW(T_ENTRY) + 4) = DSL(v); }
/* AP 628f:0039 — the entry's payload word */
void wd_list2(u16 w) { DSW(DSW(T_ENTRY) + 4) = w; }

/* AP 628f:0048 — new dated entry in the current list (sorted by date) */
void wd_list_insert(u16 date)
{
    u16 e = arena_alloc(8);
    DSW(e) = 0;
    DSW(e + 2) = date;
    if (DSW(T_LIST) == 0) DSW(e + 4) = arena_alloc((u16)(DSW(T_P) * 12));
    u16 h = (u16)(DSW(T_CUR) + 4 + 2 * DSW(T_LIST));
    DSW(h) = sorted_insert(DSW(h), e);
    DSW(T_ENTRY) = e;
}
/* AP 628f:00aa — select list l */
void wd_list3(u16 l) { DSW(T_LIST) = l; DSW(T_ENTRY) = 0; }
/* AP 628f:00bb — new target node, appended */
void wd_list_insert2(u16 id)
{
    u16 t = arena_alloc(0x0E);
    DSW(t) = 0;
    DSW(t + 2) = id;
    for (int k = 0; k < 5; k++) DSW(t + 4 + 2 * k) = 0;
    DSW(T_FIRST) = sorted_insert2(DSW(T_FIRST), t);
    DSW(T_CUR) = t;
    DSW(T_ENTRY) = 0;
    DSW(T_LIST) = 0;
}

/* AP 628f:010e */
void target_free(void)
{
    if (DSW(T_MARK)) { arena_release(DSW(T_MARK)); DSW(T_MARK) = 0; }
    DSW(T_CUR) = 0; DSW(T_FIRST) = 0; DSW(T_ENTRY) = 0; DSW(T_P) = 0;
}

/* AP 628f:0144 — TARGET.n: "INF:" (points per position), "DAT:" (targets × 5 lists) */
void target_load(u16 name)
{
    DSW(T_MARK) = arena_alloc(0);
    u16 f = chunk_open(name);
    if (!f) assert_fail(WDATA_TAG, 0x153, name);
    chunk_find(f, T_INF, 0);
    DSW(T_P) = fread_word(f);
    if (chunk_find(f, T_DAT, 0) == -1) return;                  /* without closing, as the original */
    s16 count = (s16)fread_word(f);
    u16 m = stk_mark(), v = stk_alloc(12), w = stk_alloc(4);
    for (s16 i = 0; i < count; i++) {
        wd_list_insert2(fread_word(f));
        for (s16 l = 0; l < 5; l++) {
            wd_list3((u16)l);
            s16 n = (s16)fread_word(f);
            for (s16 k = 0; k < n; k++) {
                wd_list_insert(fread_word(f));
                if (l == 0) {
                    for (s16 p = 0; p < (s16)DSW(T_P); p++) {
                        DSL(v) = fread_long(f); DSL(v + 4) = fread_long(f); DSL(v + 8) = fread_long(f);
                        wd_list0(p, v);
                    }
                } else if (l == 1) {
                    DSW(w) = fread_word(f); DSW(w + 2) = fread_word(f);
                    wd_list1(w);
                } else wd_list2(fread_word(f));
            }
        }
    }
    stk_release(m);
    chunk_close(f);
    DSW(T_CUR) = 0; DSW(T_ENTRY) = 0;
}

/* AP 628f:031f — state of the target at pos (earliest position within 10 units) at date; −1 = none */
u16 target_at_date(u16 pos, u16 date)
{
    u16 r = 0xFFFF;
    for (DSW(T_CUR) = DSW(T_FIRST); DSW(T_CUR); DSW(T_CUR) = DSW(DSW(T_CUR))) {
        DSW(T_ENTRY) = DSW(DSW(T_CUR) + 4);
        if (!lbox(DSW(DSW(T_ENTRY) + 4), pos, 10)) continue;
        for (DSW(T_ENTRY) = DSW(DSW(T_CUR) + 0x0A); DSW(T_ENTRY); DSW(T_ENTRY) = DSW(DSW(T_ENTRY))) {
            u16 e = DSW(T_ENTRY);
            if (DSW(e + 2) > date) continue;
            if (DSW(e) != 0 && DSW(DSW(e) + 2) <= date) continue;
            return DSW(e + 4);
        }
    }
    return r;
}
