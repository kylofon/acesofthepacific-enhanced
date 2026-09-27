/* AP mem.c (24b2), the near arena (24d3) and safe_malloc (0000:035c) - platform.md §5.1, §5.5.
 * PORT: the allocation log (mem_log_on DS:1F44, never set) is fatal if ever enabled. */
#include "platform/rtl_priv.h"

#include "host.h"
#include "symbols_ap.h"

#define mem_log_on  0x1F44
#define arena_base  0x1FDA
#define arena_size  0x1FDC
#define arena_lo    0x1FDE
#define arena_hi    0x1FE0

static void log_check(void) { if (DSW(mem_log_on)) host_fatal("mem.c: the allocation log is not modelled"); }

/* AP 0000:035c safe_malloc(n): 0 for FFFFh, and a block reaching the top _stklen bytes is freed */
u16 safe_malloc(u16 n)
{
    if (n == 0xFFFF) return 0;
    u16 p = rtl_malloc(n);
    if ((u16)(p + n) > (u16)(0xFFFF - DSW(AP_DS__stklen))) { rtl_free(p); return 0; }
    return p;
}

/* AP 4b9e:0000 heap_hook: an empty heap-check hook (ap_main.c) */

/* AP 24b2:0004 mem_calloc(n): n == 0 -> 1; zeroed safe_malloc */
u16 mem_calloc(u16 n)
{
    heap_hook();
    if (!n) n = 1;
    u16 p = safe_malloc(n);
    if (p) rtl_setmem_ds(p, n, 0);
    heap_hook();
    return p;
}

/* AP 24b2:004a mem_free_raw */
void mem_free_raw(u16 p)
{
    heap_hook();
    lib_free(p);
    heap_hook();
}

/* AP 24b2:0064 mem_log_reset: unlink("mem.log") (no callers) */
void mem_log_reset(void) { rtl_unlink(0x1F46 /* "mem.log" */); }

/* AP 24b2:0074 mem_new(n, tag, what, line) */
u16 mem_new(u16 n, u16 tag_ds, u16 what_ds, s16 line)
{
    (void)tag_ds; (void)what_ds; (void)line;
    u16 p = mem_calloc(n);
    log_check();
    return p;
}

/* AP 24b2:00ca mem_delete(p, tag, what, line): NULL asserts (mem.c line 68h) */
void mem_delete(u16 p_ds, u16 tag_ds, u16 what_ds, s16 line)
{
    (void)tag_ds; (void)line;
    if (!p_ds) assert_fail(far_make(SEG(0x3AF3), 0), 0x68, what_ds);
    mem_free_raw(p_ds);
    log_check();
}

/* AP 24b2:013d mem_far_new(n, flags): dos_alloc(n, (long)flags), always zeroed (except the -1 query) */
FarPtr mem_far_new(u32 n, s16 flags)
{
    FarPtr p = dos_alloc(n, (u16)flags, flags < 0 ? 0xFFFF : 0);
    heap_hook();
    if (!far_is_null(p) && n != 0xFFFFFFFFu) huge_memset_fp(p, 0, n);
    log_check();
    return p;
}

/* AP 24b2:01c0 mem_far_delete */
void mem_far_delete(FarPtr a0)
{
    if (!far_is_null(a0)) dos_free(a0);
    log_check();
}

/* AP 24d3:0002 arena_init(n) */
void arena_init(u16 n)
{
    if (n & 1) n++;
    DSW(arena_base) = rtl_malloc(n);
    DSW(arena_lo) = 0;
    DSW(arena_size) = n;
    DSW(arena_hi) = n;
}

/* AP 24d3:002d arena_done */
void arena_done(void)
{
    if (DSW(arena_base)) { rtl_free(DSW(arena_base)); DSW(arena_base) = 0; }
    DSW(arena_lo) = 0;
    DSW(arena_size) = 0;
    DSW(arena_hi) = 0xFFFF;
}

/* AP 24d3:0059 arena_alloc(n): zeroed, no overflow check */
u16 arena_alloc(u16 n)
{
    u16 lo = DSW(arena_lo);
    if (n & 1) n++;
    DSW(arena_lo) = (u16)(lo + n);
    u16 p = (u16)(DSW(arena_base) + lo);
    rtl_setmem_ds(p, n, 0);
    return p;
}

/* AP 24d3:0092 arena_release(mark) */
void arena_release(u16 mark_ds) { DSW(arena_lo) = (u16)(mark_ds - DSW(arena_base)); }

/* AP 24d3:00a1 arena_alloc_top(n) (no callers) */
u16 arena_alloc_top(u16 n)
{
    if (n & 1) n++;
    DSW(arena_hi) = (u16)(DSW(arena_hi) - n);
    return (u16)(DSW(arena_base) + DSW(arena_hi));
}

/* AP 24d3:00bb arena_release_top(mark) (no callers) */
void arena_release_top(u16 mark) { DSW(arena_hi) = (u16)(mark - DSW(arena_base)); }

/* AP 24d3:00ca arena_reset (no callers) */
void ap_arena_reset_24d3(void) { DSW(arena_lo) = 0; }

/* AP 24d3:00d5 arena_reset_top (no callers) */
void arena_reset_top(void) { DSW(arena_hi) = DSW(arena_size); }
