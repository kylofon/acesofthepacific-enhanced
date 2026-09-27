#pragma once
/* Internal glue between the platform files (not for game code). */
#include "platform/platform.h"

void timer_host_tick(void);           /* timer.c: host 236.69 Hz tick */
void timer_unhook(void);
void kbd_host_byte(u8 raw);           /* kbd.c: one byte from the keyboard (port 60h) */
void kbd_host_focus_lost(void);
void kbd_unhook(void);
void int33_host_event(int x, int y, u8 buttons);   /* int33.c: host mouse state changed */
void int33_unhook(void);
void volume_register(void);           /* volume.c: AP vol_name_cmp, vol_int24 */
void mouse_event_handler(u16 ax, u16 bx, u16 cx, u16 dx);   /* mouse.c (INT 33h fn 0Ch handler) */
void divzero_isr(void);               /* divzero.c */
