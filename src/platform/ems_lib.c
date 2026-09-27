/* The Dynamix EMS library (platform.md §5.3): INTRO 17bd (16-bit), AP 24e1/24e5 (386 build with a
 * page-count table and INT 67h/5000h). INT 67h is the ems.c model. Handles are kept in a 32-entry
 * table; the slot search is the original LOOPNE (a hit in the last slot counts as "not found"). */
#include "platform/rtl_priv.h"

#include <string.h>

#include "ems.h"
#include "prog.h"

#define AP_ems_frame   0x1FE2
#define AP_ems_pages   0x1FEE  /* u16[32], indexed like the handles */
#define AP_map_array   0x206E  /* {u16 logical, physical}[4] */

static bool is_ap(void) { return prog_current_id() == PROG_AP; }
static u16 hslot(int k) { return (u16)(lds(G_ems_handles) + 2 * k); }

/* LOOPNE over the 32 slots for value v: returns the slot index where it stopped and whether the
 * loop ended with CX = 0 (found in the last slot or not found). */
static int find_slot(u16 v, bool *cx0)
{
    int k = 0;
    u16 cx = 32;
    for (;;) {
        bool eq = DSW(hslot(k)) == v;
        cx--;
        if (cx == 0 || eq) break;
        k++;
    }
    *cx0 = cx == 0;
    return k;
}

/* INTRO 17bd:0000 / AP 24e1:0000 ems_detect: "EMMXXXX0" at the INT 67h segment:000A */
s16 lib_ems_detect(void)
{
    LB(G_ems_present) = 0;
    FarPtr v = ivt_get(0x67);
    if (memcmp(mp(v.seg, 0x0A), LP(G_ems_signature), 8) == 0) {
        LB(G_ems_present) = 1;
        if (is_ap()) { u16 seg; ems_get_frame(&seg); DSW(AP_ems_frame) = seg; }
    }
    return (s16)(s8)LB(G_ems_present);
}

/* INTRO 17bd:002a / AP 24e1:0031 ems_free_pages: INT 67h/42h BX */
u16 lib_ems_free_pages(void)
{
    u16 fr, tot;
    ems_get_page_counts(&fr, &tot);
    return fr;
}

static u16 pages_of(u32 bytes) { return (u16)((bytes + 0x3FFF) >> 14); }

/* INTRO 17bd:003a / AP 24e5:0000 ems_alloc(bytes) */
u16 lib_ems_alloc(u32 bytes)
{
    bool cx0;
    int k = find_slot(0, &cx0);
    if (cx0) return 0;                                /* PORT: the original returns AX unchanged */
    u16 pages = pages_of(bytes), h;
    u8 ah = ems_alloc(pages, &h);
    if (ah) { LB(G_ems_error) = ah; return 0; }
    DSW(hslot(k)) = h;
    if (is_ap()) DSW((u16)(AP_ems_pages + 2 * k)) = pages;
    return h;
}

/* INTRO 17bd:0080 / AP 24e5:004b ems_realloc(h, bytes) */
u16 lib_ems_realloc(u16 h, u32 bytes)
{
    u16 pages = pages_of(bytes);
    if (is_ap()) {
        bool cx0;
        int k = find_slot(h, &cx0);
        DSW((u16)(AP_ems_pages + 2 * k)) = pages;    /* the last slot when h is not found */
    }
    u8 ah = ems_realloc(h, pages);
    if (ah) { LB(G_ems_error) = ah; return 0; }
    return h;
}

/* INTRO 17bd:00b2 / AP 24e5:008d ems_free(h) */
void lib_ems_free(u16 h)
{
    if (!h) return;
    bool cx0;
    int k = find_slot(h, &cx0);
    DSW(hslot(k)) = 0;                                /* not found: the last examined slot (quirk) */
    ems_dealloc(h);
}

/* INTRO 17bd:00d9 / AP 24e5:00b6 ems_free_all (AP: atexit) */
void lib_ems_free_all(void)
{
    for (int k = 0; k < 32; k++) {
        lib_ems_free(DSW(hslot(k)));
        DSW(hslot(k)) = 0;
    }
}

static u16 map_pages(u16 h, u16 first, bool ap)
{
    if (ap) {
        bool cx0;
        int k = find_slot(h, &cx0);
        s16 n = (s16)DSW((u16)(AP_ems_pages + 2 * k));
        if (n > 4) n = 4;
        u16 pairs[8];
        for (s16 i = 0; i < n; i++) {
            DSW((u16)(AP_map_array + 4 * i)) = (u16)(first + i);
            DSW((u16)(AP_map_array + 4 * i + 2)) = (u16)i;
            pairs[2 * i] = (u16)(first + i);
            pairs[2 * i + 1] = (u16)i;
        }
        ems_map_multiple(h, pairs, n);
        return DSW(AP_ems_frame);
    }
    u16 pages;
    ems_handle_pages(h, &pages);
    s16 cx = (s16)(pages - 1);
    if (cx > 3) cx = 3;
    for (; cx >= 0; cx--) ems_map((u8)cx, (u16)(cx + first), h);
    u16 seg;
    ems_get_frame(&seg);
    return seg;
}

/* INTRO 17bd:00fa / AP 24e5:00d7 ems_map(h, first): save the map, map pages first.. at the frame */
FarPtr lib_ems_map(u16 h, u16 first)
{
    u8 ah = ems_save_map(h);
    if (ah) { LB(G_ems_error) = ah; return far_make(0, 0); }
    return far_make(map_pages(h, first, is_ap()), 0);
}

/* INTRO 17bd:013b / AP 24e5:0139 ems_restore_map */
void lib_ems_restore_map(u16 h) { ems_restore_map(h); }

/* INTRO 17bd:014b / AP 24e5:0149 ems_map_nosave(h): pages 0.. with 44h, no save */
FarPtr lib_ems_map_nosave(u16 h)
{
    return far_make(map_pages(h, 0, false), 0);
}
