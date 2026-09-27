/* SCN re-encoder (platform.md §7.3): INTRO 05da:5f97..645a, PS 16e3:5cf1.., AP 71db:0000.. Raw 8-bit
 * frames (kind -4) are re-encoded in place into the run/skip format of resources.md §4c and the DOS
 * block is shrunk. The output pointer and the source pointer advance by their offset word only (as
 * the original); the literal list is the near malloc(2000) buffer, whose stale bytes the run scan of
 * lit_flush reads past the list end (faithfully kept in DGROUP). */
#include "platform/rtl_priv.h"

#include "prog.h"

/* the encoder's DGROUP state (layout ids; INTRO DS:3128.., PS DS:740C.., AP DS:70D6..) */
#define S_pending  G_scn_pending_nl   /* s16 pending new lines */
#define S_block    G_scn_block        /* far: the pixel block */
#define S_sizepar  G_scn_size_par
#define S_src      G_scn_src          /* far */
#define S_dst      G_scn_dst          /* far */
#define S_tmp      G_scn_tmp          /* near: literal list */
static s16 run_min(void) { return LS(G_scn_run_min); }   /* 6 */
#define SW(f) DSW(lds(f))
#define SS(f) DSS(lds(f))
#define SW2(f) DSW((u16)(lds(f) + 2))                   /* segment word of a far global */
#define MASK  LB(G_scn_mask)

static void out(u8 b)
{
    u16 off = SW(S_dst), seg = SW2(S_dst);
    wr8(seg, off, b);
    SW(S_dst) = (u16)(off + 1);
}

/* INTRO 05da:607c / PS 16e3:5dd6 / AP 71db:00e7 scn_emit_skip(n) */
static void scn_emit_skip(s16 n)
{
    if (SS(S_pending)) {
        if (n < 0) {
            n = (s16)-n;
            out((u8)(n & 0x3F));
            s16 m = (s16)((n & 0x1C0) >> 6);
            if (m) out((u8)(m & 0x3F));
            while (--SS(S_pending) != 0) out(0);
            return;
        }
        while (SS(S_pending)-- != 0) out(0);
        SS(S_pending) = 0;
    }
    while (n > 0x3F) { out(0x7F); n = (s16)(n - 0x3F); }
    out((u8)(0x40 | n));
}

/* INTRO 05da:6116 / PS 16e3:5e70 / AP 71db:0181 scn_lit_out(k, list) */
static void scn_lit_out(u8 k, u8 *list)
{
    out((u8)(k | 0xC0));
    if (k & 1) { list[k] = 0; k++; }
    if (MASK == 0x0F) {
        for (u16 i = 0; k > i; i += 2) out((u8)((list[i] << 4) | list[i + 1]));
    } else {
        for (u16 i = 0; k > i; i++) out(list[i]);
    }
}

/* INTRO 05da:6196 / PS 16e3:5ef0 / AP 71db:0201 scn_lit_flush(buf, n): runs of >= run_min equal
 * values (DS:1A56 = 6) are coded as runs, the rest as literal lists of up to 63 */
static void scn_lit_flush(u16 buf, s16 n)
{
    u8 lits[0x104];
    u8 lit_n = 0;
    u16 di = buf;
    while (n > 0) {
        u16 si = di;
        u8 k = 1;
        u8 v = DSB(si++);
        while (DSB(si++) == v) k++;                   /* (not bounded by n) */
        if ((s16)k >= run_min()) {
            if ((s16)k > n) k = (u8)n;
            if (lit_n) { scn_lit_out(lit_n, lits); lit_n = 0; }
            n = (s16)(n - k);
            di = (u16)(di + k);
            while (k > 0x3F) { k = (u8)(k + 0xC1); out(0xBF); out(v); }
            if (k) { out((u8)(0x80 | k)); out(v); }
        } else {
            n--;
            lits[lit_n++] = v;
            di++;
        }
        if (lit_n == 0x3F) { scn_lit_out(lit_n, lits); lit_n = 0; }
    }
    if (lit_n) scn_lit_out(lit_n, lits);
}

/* INTRO 05da:62b4 / PS 16e3:600e / AP 71db:0322 scn_encode(frame) */
static void scn_encode(u16 fr)
{
    static u8 row[0x10000];
    s16 lit_n = 0, skip = 0;
    u8 base = 0xFF;
    u16 w = DSW((u16)(fr + 6)), h = DSW((u16)(fr + 8));
    SW(S_pending) = 0;
    SW(S_sizepar) = 0;
    SW(S_src) = DSW((u16)(fr + 2));
    SW2(S_src) = DSW(fr);
    if (MASK == 0x0F && LB(G_drv_dac_flag)) {
        for (s16 y = 0; (s16)h > y; y++)
            for (s16 x = 0; (s16)w > x; x++) {
                u8 c = rd8(SW2(S_src), SW(S_src));
                SW(S_src) = (u16)(SW(S_src) + 1);
                if (c && c < base) base = c;
            }
    } else {
        base = 1;
    }
    SW(S_src) = DSW((u16)(fr + 2));
    SW2(S_src) = DSW(fr);
    FarPtr ph = far_make(SW2(S_dst), SW(S_dst));
    SW(S_dst) = (u16)(SW(S_dst) + 1);
    for (s16 y = 0; (s16)h > y; y++) {
        u32 a = far_lin(far_make(SW2(S_src), SW(S_src)));      /* huge_memcpy of the row */
        for (u16 i = 0; i < w; i++) row[i] = mem[(a + i) % MEM_SIZE];
        SW(S_src) = (u16)(SW(S_src) + w);
        for (s16 x = 0; (s16)w > x; x++) {
            u8 c = row[x];
            if (c == 0) {
                if (lit_n) { scn_lit_flush(SW(S_tmp), lit_n); lit_n = 0; }
                skip++;
            } else {
                DSB((u16)(SW(S_tmp) + lit_n)) = (u8)((u8)(c - base) & MASK);
                lit_n++;
                if (skip) {
                    scn_emit_skip(skip);
                    skip = 0;
                } else if (SS(S_pending)) {
                    while (SS(S_pending)-- != 0) out(0);
                    SS(S_pending) = 0;
                }
            }
        }
        if (lit_n) { scn_lit_flush(SW(S_tmp), lit_n); lit_n = 0; }
        skip = (s16)(skip - w);
        SS(S_pending)++;
    }
    if (lit_n) scn_lit_flush(SW(S_tmp), lit_n);
    scn_emit_skip(0);
    wr8(ph.seg, ph.off, base);
}

/* INTRO 05da:5f97 / PS 16e3:5cf1 / AP 71db:0000 bmp_to_scn(bmp, colours) */
void bmp_to_scn(u16 bmp, u16 colours)
{
    MASK = (u8)((u8)colours - 1);
    switch (prog_current_id()) {
    case PROG_PS: SW(S_tmp) = lib_realloc(2000); break;
    case PROG_AP: SW(S_tmp) = safe_malloc(2000); break;
    default:      SW(S_tmp) = rtl_malloc(2000); break;
    }
    u16 fr0 = DSW(bmp);
    SW(S_block) = SW(S_dst) = DSW((u16)(fr0 + 2));
    SW2(S_block) = SW2(S_dst) = DSW(fr0);
    for (u16 s = bmp; DSW(s); s += 2) {
        u16 di = SW(S_dst);
        SW2(S_dst) = (u16)(SW2(S_dst) + ((s16)di >> 4));
        SW(S_dst) = di & 0x0F;
        u16 seg = SW2(S_dst), off = SW(S_dst);
        u16 fr = DSW(s);
        scn_encode(fr);
        DSW(fr) = seg;
        DSW((u16)(fr + 2)) = off;
        DSS((u16)(fr + 4)) = -2;
    }
    u16 dseg = (u16)(SW2(S_dst) - SW2(S_block));
    u16 doff = (u16)(SW(S_dst) - SW(S_block));
    SW(S_sizepar) = (u16)(dseg + ((s16)(doff + 15) >> 4));
    u16 largest;
    dos_mem_resize(DSW(DSW(bmp)), SW(S_sizepar), &largest);    /* INT 21h/4Ah, errors ignored */
    rtl_free(SW(S_tmp));
}
