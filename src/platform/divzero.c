/* Divide-overflow handler (INT 0, platform.md §3.6). PORT: C code has no traps; every overflowing
 * 16-bit DIV/IDIV is written with mem.h's idiv16_retry/div16_retry (the halve-and-retry semantics of
 * divzero_isr). The install/restore functions keep the flag and the saved vector in mem[]. */
#include "platform/rtl_priv.h"

/* INTRO 05da:4844 / PS 16e3:4b68 / AP 1d32:69a2 divzero_install */
void divzero_install(void)
{
    LB(G_divz_installed) = 1;
    FarPtr v = ivt_get(0);
    LW(G_divz_old_off) = v.off;
    LW(G_divz_old_seg) = v.seg;
    ivt_set(0, codeptr_far_shared(F_divzero_isr));
}

/* INTRO 05da:486e / PS 16e3:4b92 / AP 1d32:69cc divzero_isr: never called (see the file comment) */
void divzero_isr(void)
{
    div_unreachable("divzero_isr: a hardware divide trap (not modelled)");
}

/* INTRO 05da:48a7 / PS 16e3:4bcb / AP 1d32:6a05 divzero_restore. BUG (faithful): loads the vector
 * words instead of storing them, so INT 0 is never restored. */
void divzero_restore(void)
{
    if (!LB(G_divz_installed)) return;
    LB(G_divz_installed) = 0;
}
