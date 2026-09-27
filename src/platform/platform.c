/* Platform glue: host handlers and the per-program hardware state. */
#include "platform/plat_priv.h"

#include "codeptr.h"
#include "host.h"
#include "prog.h"
#include "sound/sound.h"
#include "symbols_intro.h"
#include "symbols_ps.h"

void platform_init(void)
{
    host_set_tick_handler(timer_host_tick);
    host_set_kbd_handler(kbd_host_byte);
    host_set_focus_lost_handler(kbd_host_focus_lost);
    host_set_mouse_handler(int33_host_event);
    /* library functions whose far addresses the programs store (vectors, hooks, callbacks) */
    volume_register();
    codeptr_register_shared(F_mouse_event_handler, (CodeFn)mouse_event_handler);
    codeptr_register_shared(F_divzero_isr, divzero_isr);
    codeptr_register(PROG_INTRO, INTRO_FN_kbd_break_hook, (CodeFn)kbd_break_hook);
    codeptr_register(PROG_PS, PS_FN_ctrl_esc_hook, (CodeFn)ctrl_esc_hook);
}

void platform_prog_start(void)
{
    timer_unhook();
    kbd_unhook();
    int33_unhook();
}

/* Every program calls sys_shutdown (kbd_restore, mouse_shutdown, timer_restore) before exit. A vector
 * left pointing into a terminated program would crash DOS; PORT: the chain loop unhooks them. The EMS,
 * the VGA state (mode, pages, DAC) and low memory survive, as in the original. */
void platform_prog_end(void)
{
    timer_unhook();
    kbd_unhook();
    int33_unhook();
    snd_host_detach();                /* also after a fatal exit through prog_exit */
}
