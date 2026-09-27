/* PS sys\helper.c page helpers 22ce:004a..00fb (video.md §4.2): read <- write page pairs, and the same
 * followed by a full-screen copy_rect. */
#include "video/vid_priv.h"

static void sel(u16 rd, u16 wr) { DRVW(D_PAGE_READ) = DRVW(rd); DRVW(D_PAGE_WRITE) = DRVW(wr); }

void page_copy_full(void)      { gfx_copy_rect(0, 0, 0x140, 0xC8); }                 /* 22ce:004a */
void pages_bg_to_back(void)    { sel(D_PAGE_BG, D_PAGE_BACK); }                      /* 22ce:0063 */
void pages_front_to_back(void) { sel(D_PAGE_FRONT, D_PAGE_BACK); }                   /* 22ce:0074 */
void pages_back_to_front(void) { sel(D_PAGE_BACK, D_PAGE_FRONT); }                   /* 22ce:0085 */
void pages_back_to_bg(void)    { sel(D_PAGE_BACK, D_PAGE_BG); }                      /* 22ce:0096 */
void copy_bg_to_back(void)     { sel(D_PAGE_BG, D_PAGE_BACK); page_copy_full(); }    /* 22ce:00a7 */
void copy_back_to_bg(void)     { sel(D_PAGE_BACK, D_PAGE_BG); page_copy_full(); }    /* 22ce:00bc */
void copy_bg_to_front(void)    { sel(D_PAGE_BG, D_PAGE_FRONT); page_copy_full(); }   /* 22ce:00d1 */
void copy_back_to_front(void)  { sel(D_PAGE_BACK, D_PAGE_FRONT); page_copy_full(); } /* 22ce:00e6 */
void copy_front_to_back(void)  { sel(D_PAGE_FRONT, D_PAGE_BACK); page_copy_full(); } /* 22ce:00fb */
