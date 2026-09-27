/* LIM EMS 4.0 model, see ems.h. */
#include "ems.h"
#include "mem.h"
#include "host.h"

#include <SDL3/SDL.h>
#include <string.h>

#define PAGE 0x4000u

typedef struct {
    bool used;
    u16  pages;
    u8  *data;                          /* pages * 16 KB */
    bool saved;                         /* 47h context */
    s32  saved_map[4][2];               /* {handle, logical} per physical page, -1 = unmapped */
} Handle;

static Handle handles[EMS_MAX_HANDLES];
static s32 phys[4][2];                  /* current mapping: {handle, logical}, handle -1 = unmapped */
static u16 pages_used;

static u8 *frame(int p) { return mp(EMS_FRAME_SEG, (u16)(p * PAGE)); }

void ems_init(void)
{
    for (int i = 0; i < EMS_MAX_HANDLES; i++) {
        SDL_free(handles[i].data);
        memset(&handles[i], 0, sizeof handles[i]);
    }
    handles[0].used = true;                                 /* OS handle, 0 pages */
    for (int p = 0; p < 4; p++) phys[p][0] = -1;
    pages_used = 0;
    memcpy(mp(EMS_DEV_SEG, 0x0A), "EMMXXXX0", 8);
    wr8(EMS_DEV_SEG, 0, 0xCF);                               /* IRET: nothing executes it */
    ivt_set(0x67, far_make(EMS_DEV_SEG, 0));
}

static void write_back(void)
{
    for (int p = 0; p < 4; p++) {
        if (phys[p][0] < 0) continue;
        Handle *h = &handles[phys[p][0]];
        memcpy(h->data + (u32)phys[p][1] * PAGE, frame(p), PAGE);
    }
}

static void copy_in(int p)
{
    if (phys[p][0] < 0) return;
    Handle *h = &handles[phys[p][0]];
    memcpy(frame(p), h->data + (u32)phys[p][1] * PAGE, PAGE);
}

void ems_sync(void) { write_back(); }

static bool valid(u16 handle) { return handle < EMS_MAX_HANDLES && handles[handle].used; }

u8 ems_status(void) { return 0; }

u8 ems_get_frame(u16 *seg) { *seg = EMS_FRAME_SEG; return 0; }

u8 ems_get_page_counts(u16 *free_pages, u16 *total_pages)
{
    if (free_pages) *free_pages = (u16)(EMS_TOTAL_PAGES - pages_used);
    if (total_pages) *total_pages = EMS_TOTAL_PAGES;
    return 0;
}

u8 ems_alloc(u16 pages, u16 *handle)
{
    if (pages > EMS_TOTAL_PAGES) return 0x87;
    if (pages > EMS_TOTAL_PAGES - pages_used) return 0x88;
    if (pages == 0) return 0x89;                             /* LIM 4.0: 43h with 0 pages */
    for (u16 i = 1; i < EMS_MAX_HANDLES; i++) {
        if (handles[i].used) continue;
        handles[i].data = SDL_calloc(pages, PAGE);
        if (!handles[i].data) host_fatal("out of memory (EMS)");
        handles[i].used = true;
        handles[i].pages = pages;
        handles[i].saved = false;
        pages_used = (u16)(pages_used + pages);
        *handle = i;
        return 0;
    }
    return 0x85;
}

u8 ems_map(u8 physical, u16 logical, u16 handle)
{
    if (!valid(handle)) return 0x83;
    if (physical > 3) return 0x8B;
    if (logical != 0xFFFF && logical >= handles[handle].pages) return 0x8A;
    write_back();
    if (logical == 0xFFFF) { phys[physical][0] = -1; return 0; }
    phys[physical][0] = handle;
    phys[physical][1] = logical;
    for (int p = 0; p < 4; p++) copy_in(p);                  /* aliases see the written-back data */
    return 0;
}

u8 ems_dealloc(u16 handle)
{
    if (handle == 0 || !valid(handle)) return 0x83;
    if (handles[handle].saved) return 0x86;
    write_back();
    for (int p = 0; p < 4; p++)
        if (phys[p][0] == handle) phys[p][0] = -1;
    pages_used = (u16)(pages_used - handles[handle].pages);
    SDL_free(handles[handle].data);
    memset(&handles[handle], 0, sizeof handles[handle]);
    return 0;
}

u8 ems_version(u8 *bcd) { *bcd = 0x40; return 0; }

u8 ems_save_map(u16 handle)
{
    if (!valid(handle)) return 0x83;
    if (handles[handle].saved) return 0x8D;
    memcpy(handles[handle].saved_map, phys, sizeof phys);
    handles[handle].saved = true;
    return 0;
}

u8 ems_restore_map(u16 handle)
{
    if (!valid(handle)) return 0x83;
    if (!handles[handle].saved) return 0x8E;
    write_back();
    memcpy(phys, handles[handle].saved_map, sizeof phys);
    for (int p = 0; p < 4; p++) {
        if (phys[p][0] >= 0 && (!valid((u16)phys[p][0]) || phys[p][1] >= handles[phys[p][0]].pages))
            phys[p][0] = -1;                                  /* owner freed or shrunk meanwhile */
        copy_in(p);
    }
    handles[handle].saved = false;
    return 0;
}

u8 ems_handle_count(u16 *count)
{
    u16 n = 0;
    for (int i = 0; i < EMS_MAX_HANDLES; i++) n = (u16)(n + handles[i].used);
    *count = n;
    return 0;
}

u8 ems_handle_pages(u16 handle, u16 *pages)
{
    if (!valid(handle)) return 0x83;
    *pages = handles[handle].pages;
    return 0;
}

u8 ems_map_multiple(u16 handle, const u16 *pairs, int n)
{
    if (!valid(handle)) return 0x83;
    for (int i = 0; i < n; i++) {
        u16 logical = pairs[2 * i], physical = pairs[2 * i + 1];
        if (physical > 3) return 0x8B;
        if (logical != 0xFFFF && logical >= handles[handle].pages) return 0x8A;
    }
    write_back();
    for (int i = 0; i < n; i++) {
        u16 logical = pairs[2 * i], physical = pairs[2 * i + 1];
        phys[physical][0] = logical == 0xFFFF ? -1 : handle;
        phys[physical][1] = logical;
    }
    for (int p = 0; p < 4; p++) copy_in(p);
    return 0;
}

u8 ems_realloc(u16 handle, u16 pages)
{
    if (!valid(handle)) return 0x83;
    Handle *h = &handles[handle];
    if (pages > h->pages && pages - h->pages > EMS_TOTAL_PAGES - pages_used) return 0x88;
    write_back();
    u8 *d = SDL_realloc(h->data, (size_t)(pages ? pages : 1) * PAGE);
    if (!d) host_fatal("out of memory (EMS)");
    if (pages > h->pages) memset(d + (u32)h->pages * PAGE, 0, (u32)(pages - h->pages) * PAGE);
    h->data = d;
    pages_used = (u16)(pages_used - h->pages + pages);
    h->pages = pages;
    for (int p = 0; p < 4; p++)
        if (phys[p][0] == handle && phys[p][1] >= pages) phys[p][0] = -1;
    return 0;
}
