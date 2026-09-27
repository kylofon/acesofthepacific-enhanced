/* Windows helpers (INT 2Fh, platform.md §3.8). PORT: no Windows - win_detect is 0, so the (dead)
 * task-switch path never runs; the helpers are kept as no-ops. */
#include "platform/rtl_priv.h"

/* INTRO 0db2:0002 / PS 1671:0007 / AP 247d:0005 win_detect: INT 2Fh 1600h / 4680h -> not Windows */
s16 win_detect(void) { return 0; }

/* INTRO 0db2:0035 / PS 1671:003a / AP 247d:0034 win_critical_section(leave): INT 2Fh 1681h/1682h */
void win_critical_section(s16 leave) { (void)leave; }

/* INTRO 0db2:004c / PS 1671:0051 / AP 247d:004b win_switch_out(tag): clipboard "*~"+tag, focus the
 * system VM (INT 2Fh 1701h/1703h/1708h/168Bh). Unreachable (win_detect = 0). */
void win_switch_out(u16 tag_ds) { (void)tag_ds; }

/* PS 1671:00fd / AP 247d:00ee win_clip_put(tag): the same without 168Bh; no callers */
void win_clip_put(u16 tag_ds) { (void)tag_ds; }

/* INTRO 0dc0:000d / PS 166d:0009 / AP 2466:0118 win_task_switch: the caller-side wrapper (sound
 * suspend, switch, resume). PORT: only reached after win_detect() != 0 in the original; no-op. */
void win_task_switch(void) { }
