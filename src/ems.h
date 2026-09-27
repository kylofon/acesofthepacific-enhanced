#pragma once
/* LIM EMS 4.0 model (INT 67h replacement; platform.md §5.3, §5.6, PLAN.md decision 5).
 *
 * The page frame is a real 64 KB window in mem[] at EMS_FRAME_SEG:0000 (4 physical pages of 16 KB).
 * Every handle owns a host buffer of pages x 16 KB. Mapping is copy-in / copy-out: before any mapping
 * change the four physical pages are written back to the logical pages they show, then the new logical
 * pages are copied in. This is exact as long as nothing keeps a pointer into the window across a remap,
 * which is how the original uses EMS (every user maps its object at the frame start, §5.6).
 *
 * The device header "EMMXXXX0" is at EMS_DEV_SEG:000A and INT 67h points there, so the ported
 * ems_detect (INT 21h/3567h + compare at ES:000A) finds it. The functions return the INT 67h status
 * (AH): 0 = success, 80h.. = errors as in the LIM specification. Handles are 1..EMS_MAX_HANDLES-1
 * (handle 0 is the operating system handle, as LIM defines). EMS survives program switches (DOS keeps
 * EMS allocations of a terminated program; AP frees its own with atexit(ems_free_all)). */
#include "types.h"

#define EMS_TOTAL_PAGES  256        /* 4 MB: >= 33 free pages -> AP runs its tier-4 layout (§9.3) */
#define EMS_MAX_HANDLES  64
#define EMS_DEV_SEG      0xF800     /* fake EMM driver header segment */

void ems_init(void);                /* once, after mem_init_machine(): device header, INT 67h vector */

u8 ems_status(void);                                         /* 40h */
u8 ems_get_frame(u16 *seg);                                  /* 41h */
u8 ems_get_page_counts(u16 *free_pages, u16 *total_pages);   /* 42h */
u8 ems_alloc(u16 pages, u16 *handle);                        /* 43h */
u8 ems_map(u8 physical, u16 logical, u16 handle);            /* 44h (logical FFFFh = unmap) */
u8 ems_dealloc(u16 handle);                                  /* 45h */
u8 ems_version(u8 *bcd);                                     /* 46h: 40h */
u8 ems_save_map(u16 handle);                                 /* 47h */
u8 ems_restore_map(u16 handle);                              /* 48h */
u8 ems_handle_count(u16 *count);                             /* 4Bh */
u8 ems_handle_pages(u16 handle, u16 *pages);                 /* 4Ch */
u8 ems_map_multiple(u16 handle, const u16 *pairs, int n);    /* 5000h: {logical, physical} pairs */
u8 ems_realloc(u16 handle, u16 pages);                       /* 51h */

/* Port helper: write the window back to its owners (e.g. before inspecting handle memory). */
void ems_sync(void);
