/* Timer: PIT channel 0 / INT 8 (platform.md §4), shared library code for INTRO, PS and AP.
 *
 * The host runs a fixed 236.69 Hz tick (PIT divisor 5041 = timer_install(13), the only value any
 * program uses). Each tick: if INT 8 points at timer_isr, run it (countdown, callbacks in slot order,
 * BIOS tick every 13th); otherwise only the BIOS tick (every 13th host tick = 18.2 Hz). */
#include "platform/plat_priv.h"

#include "host.h"

static bool int8_hooked;              /* INT 8 vector = timer_isr (the saved vector is the BIOS one) */
static int bios_div;                  /* host ticks towards the next BIOS tick while INT 8 is not hooked */

bool timer_hooked(void) { return int8_hooked; }

/* BIOS INT 8: 40:6C tick count, midnight flag 40:70 (1800B0h ticks per day). PORT: no EOI, no INT 1Ch
 * chaining (the INT 1Ch ramp handler is never installed, §3.3.7), no floppy motor count. */
static void bios_int8(void)
{
    u32 t = rd32(0x40, 0x6C) + 1;
    if (t >= 0x1800B0u) { t = 0; wr8(0x40, 0x70, 1); }
    wr32(0x40, 0x6C, t);
}

/* 05da:2b08 / 16e3:311c / 1d32:4f56 */
int is_tandy(void)
{
    if (rd8(0xF000, 0xFFFE) == 0xFF && rd8(0xF000, 0xC000) == 0x21) LB(G_is_tandy) = 1;
    return (int)(s8)LB(G_is_tandy);
}

/* 05da:2374 / 16e3:2988 / 1d32:47c2 */
int timer_install(int mult)
{
    if (LB(G_timer_installed)) return 0;
    LW(G_timer_mask) = 0;
    is_tandy();                                       /* result unused */
    /* (INT 21h/3508h: the old vector is saved in the code segment; PORT: it is the BIOS handler) */
    if ((s16)mult > 255 || mult == 0) return 0;
    LW(G_timer_bios_reload) = LW(G_timer_bios_count) = (u16)mult;
    LW(G_timer_divisor) = (u16)(0xFFFFu / (u16)mult);
    /* PORT: PIT programming (43h/40h) and the IRQ mask (21h) are replaced by the host's fixed tick. */
    if (LW(G_timer_divisor) != PIT_DIV_GAME)
        host_fatal("timer_install(%d): only 13 (236.69 Hz) is modelled", mult);
    int8_hooked = true;
    LB(G_timer_installed) = 1;
    return 1;
}

/* 05da:23e1 / 16e3:29f5 / 1d32:482f */
int timer_restore(void)
{
    if (!LB(G_timer_installed)) return 0;
    /* PORT: PIT back to 65536 (18.2 Hz) = BIOS ticks every 13th host tick */
    int8_hooked = false;
    bios_div = 0;
    LB(G_timer_installed) = 0;
    return 1;
}

/* 05da:2307 / 16e3:291b / 1d32:4755 */
int timer_add(FarPtr fn, int rate)
{
    if (!LB(G_timer_installed)) return 0;
    u16 mask = LW(G_timer_mask);
    if (mask == 0xFFFF) return 0;
    int i = 0;
    while (mask & (1u << i)) i++;
    u16 cnt = (u16)(lds(G_timer_cnt) + 4 * i);
    DSW(cnt + 2) = DSW(cnt) = (u16)rate;             /* reload, count */
    ds_far_wr((u16)(lds(G_timer_fn) + 4 * i), fn);
    LW(G_timer_mask) = (u16)(mask | (1u << i));      /* cli; ... sti */
    return i + 1;
}

/* 05da:2351 / 16e3:2965 / 1d32:479f */
int timer_remove(int handle)
{
    u16 i = (u16)(handle - 1);
    if (i & 0xF0) return 0;                           /* only the low byte is tested */
    LW(G_timer_mask) &= (u16)~(1u << (i & 0x0F));
    return 1;
}

/* 05da:241a / 16e3:2a2e / 1d32:4868 */
void timer_isr(void)
{
    s16 c = (s16)(LS(G_timer_countdown) - 1);
    LS(G_timer_countdown) = c < 0 ? (s16)~c : c;     /* saturates at 0; a negative v becomes -v (quirk) */
    u16 m = LW(G_timer_mask);
    for (int i = 0; m; i++, m >>= 1) {
        if (!(m & 1)) continue;
        u16 cnt = (u16)(lds(G_timer_cnt) + 4 * i);
        DSW(cnt) = (u16)(DSW(cnt) - 1);
        if (DSW(cnt) == 0) {
            CodeFn f = codeptr_lookup(ds_far((u16)(lds(G_timer_fn) + 4 * i)));
            if (f) f();                               /* far call, IF = 0, before the EOI */
            DSW(cnt) = DSW(cnt + 2);
        }
    }
    LW(G_timer_bios_count) = (u16)(LW(G_timer_bios_count) - 1);
    if (LW(G_timer_bios_count) != 0) return;         /* EOI */
    LW(G_timer_bios_count) = LW(G_timer_bios_reload);
    bios_int8();                                      /* jmp far old_int8 */
}

/* Host tick (236.69 Hz). */
void timer_host_tick(void)
{
    if (int8_hooked && prog_layout) {
        timer_isr();
        return;
    }
    if (++bios_div >= 13) { bios_div = 0; bios_int8(); }
}

void timer_unhook(void) { int8_hooked = false; bios_div = 0; }
