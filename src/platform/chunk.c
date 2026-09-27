/* Chunk access (platform_resources.md §6.3): INTRO 05da:5764.., PS 16e3:54be.., AP 7187. A chunk file
 * is a my_fopen'ed entry holding a tagged chunk tree; at most 4 are open, their 43h-byte state in
 * chunk_slots[4] (DGROUP), one backup record for failed searches. */
#include "platform/rtl_priv.h"

#include <string.h>

/* CHF (43h bytes) */
#define CH_h      0x00
#define CH_path   0x02
#define CH_end    0x1B
#define CH_len    0x37
#define CH_count  0x39
#define CH_pos    0x3B
#define CH_size   0x3F
#define CH_SIZE   0x43

#define SW(s, f) DSW((u16)((s) + (f)))
#define SS(s, f) DSS((u16)((s) + (f)))
#define SL(s, f) DSL((u16)((s) + (f)))
#define END(s, i) DSL((u16)((s) + CH_end + 4 * (i)))

static void scopy(u16 dst, u16 src) { memmove(DSP(dst), DSP(src), CH_SIZE); }   /* SCOPY@ */

/* INTRO 05da:5764 / PS 16e3:54be / AP 7187:0000 chunk_slot: the slot of h (h = 0: a free one), 3..0 */
static u16 chunk_slot(u16 h)
{
    for (s16 i = 3; i >= 0; i--) {
        u16 s = (u16)(lds(G_chunk_slots) + CH_SIZE * i);
        if (SW(s, CH_h) == h) return s;
    }
    return 0;
}

/* INTRO 05da:5795 / PS 16e3:54ef / AP 7187:0031 chunk_reset: zero all but the handle and end[0] */
static void chunk_reset(u16 s)
{
    u16 h = SW(s, CH_h);
    u32 e0 = END(s, 0);
    memset(DSP(s), 0, CH_SIZE);
    END(s, 0) = e0;
    SW(s, CH_h) = h;
    my_rewind(h);
}

/* INTRO 05da:57e2 / PS 16e3:553c / AP 7187:007e chunk_path_eq */
static s16 chunk_path_eq(u16 a, u16 b, s16 n)
{
    while ((DSB(a) || DSB(b)) && n-- != 0)
        if (DSB(a++) != DSB(b++)) return 0;
    return 1;
}

/* INTRO 05da:581a / PS 16e3:5574 / AP 7187:00b6 chunk_save(dst, h) */
u16 chunk_save(u16 dst_ds, u16 p1_ds)
{
    if (!p1_ds || !dst_ds) return 0;
    u16 s = chunk_slot(p1_ds);
    if (!s) return 0;
    scopy(dst_ds, s);
    return dst_ds;
}

/* INTRO 05da:5856 / PS 16e3:55b0 / AP 7187:00f3 chunk_restore(src) */
s16 chunk_restore(u16 src_ds)
{
    if (!src_ds || !SW(src_ds, CH_h)) return 0;
    u16 s = chunk_slot(SW(src_ds, CH_h));
    if (!s) return 0;
    scopy(s, src_ds);
    my_fseek(SW(s, CH_h), (s32)SL(s, CH_pos), 0);
    return 1;
}

/* INTRO 05da:589e / PS 16e3:55f8 / AP 7187:013c chunk_open(name) */
u16 chunk_open(u16 name_ds)
{
    u16 s = chunk_slot(0);
    if (!s) return 0;
    u16 m = stk_mark();
    u16 rb = stk_alloc(4);
    memcpy(DSP(rb), "rb", 3);
    SW(s, CH_h) = my_fopen(name_ds, rb);
    stk_release(m);
    if (!SW(s, CH_h)) return 0;
    my_fseek(SW(s, CH_h), 0, 2);
    END(s, 0) = (u32)my_ftell(SW(s, CH_h)) | 0x80000000u;
    chunk_reset(s);
    return SW(s, CH_h);
}

/* INTRO 05da:5902 / PS 16e3:565c / AP 7187:01a2 chunk_fail: restore the backup, reseek, -1 */
static s32 chunk_fail(u16 s)
{
    scopy(s, lds(G_chunk_backup));
    my_fseek(SW(s, CH_h), (s32)SL(s, CH_pos), 0);
    return -1;
}

/* INTRO 05da:5934 / PS 16e3:568e / AP 7187:01d4 chunk_find(h, path, n): n >= 1 the n-th occurrence
 * from the file start, 0 the next one (or the same chunk if nothing was read since), -1 back to the
 * current chunk. Returns the payload offset (file positioned there) or -1. */
s32 chunk_find(u16 p0_ds, u16 path_ds, s16 n)
{
    u16 h = p0_ds, s;
    if (!h || !(s = chunk_slot(h))) return -1;
    s16 plen = (s16)ds_strlen(path_ds);
    if (plen == 0 || (plen & 3)) return -1;
    scopy(lds(G_chunk_backup), s);
    s16 want;
    if (chunk_path_eq(path_ds, (u16)(s + CH_path), 25)) {
        if (n == 0 && (u32)my_ftell(h) == SL(s, CH_pos)) return (s32)SL(s, CH_pos);
        if (n == -1) { my_fseek(h, (s32)SL(s, CH_pos), 0); return (s32)SL(s, CH_pos); }
        if (SS(s, CH_count) != 0) {
            if (n != 0) {
                want = n;
                if (SS(s, CH_count) < n) n = (s16)(n - SS(s, CH_count));
                else if (SS(s, CH_count) > n) chunk_reset(s);
                else { my_fseek(h, (s32)SL(s, CH_pos), 0); return (s32)SL(s, CH_pos); }
            } else {
                n = 1;
                want = (s16)(SS(s, CH_count) + 1);
            }
        } else {
            want = n;
            if (n) chunk_reset(s);
            else n = 1;
        }
    } else if (n > 0) {
        chunk_reset(s);
        want = n;
    } else {
        n = 1;
        want = 0;
    }
    if (!(END(s, SS(s, CH_len) >> 2) & 0x80000000u)) SL(s, CH_pos) += SL(s, CH_size);   /* skip a leaf */
    my_fseek(h, (s32)SL(s, CH_pos), 0);
    while (n-- != 0) {
        do {
            for (;;) {
                while ((END(s, SS(s, CH_len) >> 2) & 0x7FFFFFFFu) == SL(s, CH_pos)) {
                    if (SS(s, CH_len) == 0) return chunk_fail(s);
                    SS(s, CH_len) = (s16)(SS(s, CH_len) - 4);
                }
                if (END(s, SS(s, CH_len) >> 2) & 0x80000000u) break;
                SL(s, CH_pos) += SL(s, CH_size);
                my_fseek(h, (s32)SL(s, CH_pos), 0);
            }
            if (my_fread((u16)(s + CH_path + SS(s, CH_len)), 1, 4, h) != 4) return chunk_fail(s);
            SS(s, CH_len) = (s16)(SS(s, CH_len) + 4);
            if (SS(s, CH_len) >= 0x18) return chunk_fail(s);
            DSB((u16)(s + CH_path + SS(s, CH_len))) = 0;
            SL(s, CH_pos) += 8;
            if (my_fread((u16)(s + CH_size), 4, 1, h) != 1) return chunk_fail(s);
            END(s, SS(s, CH_len) >> 2) = SL(s, CH_pos) + SL(s, CH_size);
            SL(s, CH_size) &= 0x7FFFFFFFu;
            if ((s32)SL(s, CH_size) < 0 || SL(s, CH_size) >= (END(s, 0) & 0x7FFFFFFFu)) return chunk_fail(s);
        } while (!(SS(s, CH_len) == plen && chunk_path_eq((u16)(s + CH_path), path_ds, plen)));
    }
    SS(s, CH_count) = want;
    return (s32)SL(s, CH_pos);
}

/* INTRO 05da:5c21 / PS 16e3:597b / AP 7187:04cc chunk_size */
s32 chunk_size(u16 p0_ds)
{
    u16 s;
    if (!p0_ds || !(s = chunk_slot(p0_ds))) return -1;
    return (s32)SL(s, CH_size);
}

/* INTRO 05da:5c4b / PS 16e3:59a5 / AP 7187:04f7 chunk_close */
s16 chunk_close(u16 p0_ds)
{
    u16 s;
    if (!p0_ds || !(s = chunk_slot(p0_ds))) return 0;
    SW(s, CH_h) = 0;
    my_fclose(p0_ds);
    return 1;
}

/* INTRO 05da:5c7a / PS 16e3:59d4 / AP 7187:0527 chunk_is_open */
s16 chunk_is_open(u16 p0_ds) { return chunk_slot(p0_ds) ? 1 : 0; }
