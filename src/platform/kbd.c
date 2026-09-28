/* Keyboard: INT 9 (platform.md §3.3), shared library code for INTRO, PS and AP.
 *
 * The host delivers the XT set-1 byte stream; each byte is one IRQ1: if INT 9 points at kbd_isr the
 * ported ISR runs (INTRO/PS: BIOS type-ahead buffer at 40:1E..3D; AP: its private 1-entry queue),
 * otherwise a minimal BIOS INT 9 fills the buffer. PORT: port 61h acknowledge, EOIs and the Tandy paths
 * (is_tandy() is 0 on the modelled AT BIOS, §3.3.8) are dropped. */
#include "platform/plat_priv.h"

#include "host.h"
#include "enhanced/enhanced.h"   /* ENH: held direction keys combined */
#include "prog.h"
#include "symbols_ap.h"
#include "symbols_intro.h"
#include "symbols_ps.h"

static bool int9_hooked;              /* INT 9 vector = kbd_isr */

#define BDA 0x40
#define SHIFT_FLAGS 0x17

/* 05da:0014 / 16e3:62c9 / 1d32:0668 */
int kbd_install(int hook_1c)
{
    if (!LB(G_kbd_installed)) {
        /* INT 21h/3509h, 351Ch: old vectors saved in the code segment; INT 21h/2509h -> kbd_isr */
        int9_hooked = true;
        if (hook_1c) host_fatal("kbd_install(1): the INT 1Ch ramp handler is not ported (never used, 3.3.7)");
        LB(G_kbd_enhanced) = 0;
        if (is_tandy())                                   /* PORT: Tandy 1000 paths not ported */
            host_fatal("kbd_install: Tandy keyboard paths are not ported");
        LB(G_kbd_installed) = 1;
    }
    wr8(BDA, SHIFT_FLAGS, rd8(BDA, SHIFT_FLAGS) & (u8)~0x20);   /* NumLock off (on every call) */
    if (LB(G_kbd_caps_forced)) wr8(BDA, SHIFT_FLAGS, rd8(BDA, SHIFT_FLAGS) | 0x40);
    return LB(G_kbd_installed);
}

/* 05da:00d9 / 16e3:638d / 1d32:072d */
int kbd_restore(void)
{
    if (!LB(G_kbd_installed)) return 0;
    LB(G_kbd_installed) = 0;
    wr16(BDA, 0x1A, rd16(BDA, 0x1C));                     /* flush the BIOS buffer */
    int9_hooked = false;                                  /* INT 9 / INT 1Ch vectors restored */
    return 1;
}

/* Ctrl+Esc hook of the INTRO/PS ISR: INTRO 05bb:0006 kbd_break_hook (empty), PS 227c:0034
 * ctrl_esc_hook (sets the quit flag DS:00C1). Called through codeptr: the owning module registers it. */
static void call_ctrl_esc_hook(void)
{
    u32 fn = prog_current_id() == PROG_INTRO ? INTRO_FN_kbd_break_hook : PS_FN_ctrl_esc_hook;
    void (*hook)(int) = (void (*)(int))codeptr_lookup(codeptr_far(fn));
    hook(0);
}

/* 05da:0117 / 16e3:63cb (INTRO/PS) and 1d32:075e (AP): identical up to kbd_last_key = key. */
void kbd_isr(u8 raw)
{
    u8 al = raw;                                          /* in al, 60h */
    u8 sc = al & 0x7F, brk = al >> 7;
    if (sc >= 0x59) return;                               /* E0/E1 prefixes and >= 59h ignored */
    u8 *ks = LP(G_key_state);
    u8 old = ks[sc];
    ks[sc] = (u8)((old & (0xFE | brk)) ^ 1);              /* a break without a make SETS bit 0 (quirk) */
    u8 *kdir = LP(G_kbd_dir);
    u8 flags = rd8(BDA, SHIFT_FLAGS);
    if (al & 0x80) {                                      /* ---- break ---- */
        if (old & 0xF8) {
            int set = (old >> 3) & 1;
            if (kdir[set] == (old >> 4)) kdir[set] = 0;
        }
        LW(G_kbd_last_key) = 0;
        u8 v = LP(G_kbd_ascii)[sc];
        if ((v & 0x80) && !(v & 0x70)) wr8(BDA, SHIFT_FLAGS, flags & (u8)(v ^ 0x7F));
        return;
    }
    /* ---- make ---- */
    if (old & 0xF8) kdir[(old >> 3) & 1] = (u8)(old >> 4);
    u8 v = LP(G_kbd_ascii)[sc];
    if (v & 0x80) {
        v &= 0x7F;
        if (!(v & 0x70)) { wr8(BDA, SHIFT_FLAGS, flags | v); return; }   /* Shift/Ctrl/Alt */
        if ((v & 0x40) && LB(G_kbd_caps_forced)) return;
        if (old & 1) return;                              /* typematic repeat: no toggle */
        wr8(BDA, SHIFT_FLAGS, flags ^ v);                 /* CapsLock toggle */
        return;
    }
    if (flags & 0x04) {                                   /* Ctrl: uppercase | 80h (Ctrl+A = C1h) */
        v |= 0x80;
        if (old & 4) v = (u8)(v - 0x20);
    } else if ((flags & 0x40) && (old & 4)) {
        v = (u8)(v - 0x20);                               /* CapsLock + letter (Shift ignored) */
    } else if (flags & 0x03) {
        v = LP(G_kbd_shifted)[sc];
    }
    u16 key = (u16)(sc << 8 | v);
    LW(G_kbd_last_key) = key;

    if (prog_current_id() == PROG_AP) {                   /* ---- 1d32:075e tail ---- */
        if (DSS(AP_DS_kbq_count) < DSS(AP_DS_kbq_limit)) {   /* kbq_limit = 1: one key */
            u16 e = key;
            if (DSB(AP_DS_kbq_ascii_mode) == 0) e = (u16)((e & 0xFF00) | rd8(BDA, SHIFT_FLAGS));
            DSW(AP_DS_kbq_buf + DSW(AP_DS_kbq_tail)) = e;
            DSW(AP_DS_kbq_tail) = (u16)((DSW(AP_DS_kbq_tail) + 2) & 0x3F);
            DSW(AP_DS_kbq_count) = (u16)(DSW(AP_DS_kbq_count) + 1);
        }
        flags = rd8(BDA, SHIFT_FLAGS);
        if ((key >> 8) == 0x20 && (flags & 4)) return;    /* Ctrl+D: one EOI */
        if (flags & 4) {
            if (key == 0x019B) DSB(AP_DS_ap_quit_request) = 1;                        /* Ctrl+Esc */
            else if ((flags & 8) && (key >> 8) == 0x53) DSB(AP_DS_ap_quit_request) = 2;   /* Ctrl+Alt+Del */
        }
        return;                                           /* (dead Ctrl+P -> INT 5 at 1d32:091f) */
    }

    /* ---- 05da:0117 / 16e3:63cb tail: BIOS type-ahead insert, hard-coded ring 1Eh..3Ch ---- */
    u16 head = rd16(BDA, 0x1A), tail = rd16(BDA, 0x1C);
    bool skip = head == 0x3C ? tail == 0x1E : (u16)(head + 2) == tail;
    /* BUG (faithful): tests "exactly one key pending", not "buffer full" (§3.3.4) */
    if (!skip) {
        wr16(BDA, tail, key);
        tail = tail == 0x3C ? 0x1E : (u16)(tail + 2);
        wr16(BDA, 0x1C, tail);
    }
    flags = rd8(BDA, SHIFT_FLAGS);
    /* Ctrl+D: early EOI (PORT: no PIC) */
    if ((key >> 8) == 0x0F) return;                       /* Tab */
    if (key == 0x019B && (flags & 4)) {                   /* Ctrl+Esc */
        u16 t = (u16)(tail - 2);
        if (t == 0x1C) t = 0x3C;
        wr16(BDA, t, 0);                                  /* zero the slot just written */
        wr16(BDA, 0x1A, rd16(BDA, 0x1C));                 /* flush */
        call_ctrl_esc_hook();
    }
}

/* 05da:03ab / 16e3:6658 */
u16 key_read(void)
{
    u16 head = rd16(BDA, 0x1A);
    if (head == rd16(BDA, 0x1C)) return 0;
    u16 ax = rd16(BDA, head);
    head = (u16)(head + 2);
    if (head == rd16(BDA, 0x82)) head = rd16(BDA, 0x80);
    wr16(BDA, 0x1A, head);
    return ax;
}

/* 1d32:0939 */
u16 key_queue_read(void)
{
    if (DSW(AP_DS_kbq_count) == 0) return 0;
    u16 e = DSW(AP_DS_kbq_buf + DSW(AP_DS_kbq_head));
    DSW(AP_DS_kbq_head) = (u16)((DSW(AP_DS_kbq_head) + 2) & 0x3F);
    DSW(AP_DS_kbq_count) = (u16)(DSW(AP_DS_kbq_count) - 1);
    return e;
}

/* 05da:03da / 16e3:6687 / 1d32:095e */
int kbd_dir_bits(int set)
{
    if (enh_enabled()) {
        /* ENH: the original keeps one direction per set (the last key pressed), so a second key held
         * with the first (Left + Down to pull through a turn) does nothing until the first is let go.
         * Here every held key of the set counts: key state bits 7..4 = direction code, bit 3 = set,
         * bit 0 = held; the table gives up 1, down 2, left 4, right 8. Opposite keys cancel. */
        const u8 *ks = LP(G_key_state), *tab = LP(G_dir_bits_tab);
        int bits = 0;
        for (int sc = 0; sc < 0x59; sc++) {
            u8 s = ks[sc];
            if ((s & 0xF0) && (s & 1) && ((s >> 3) & 1) == (set != 0)) bits |= tab[s >> 4];
        }
        if ((bits & 3) == 3) bits &= ~3;
        if ((bits & 12) == 12) bits &= ~12;
        return bits;
    }
    return LP(G_dir_bits_tab)[LP(G_kbd_dir)[set != 0]];
}

/* 05da:03f4 / 16e3:66a1 / 1d32:0978 (no range check) */
int key_held(int scancode)
{
    return DSB(lds(G_key_state) + scancode) & 1;
}

/* 1d32:098c */
u8 key_name_char(int scancode)
{
    return (unsigned)(scancode - 2) <= 9 ? DSB(AP_DS_kbd_ascii + scancode) : DSB(AP_DS_kbd_shifted + scancode);
}

/* ------------------------------------------------------------------ BIOS INT 9 / INT 16h model */

/* The library tables (identical in the three programs, platform.md §3.3.1) serve as the BIOS
 * translation; the BIOS inserts only when the buffer is not full. */
static const u8 bios_ascii[0x59] = {
    0x00, 0x1B, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x2D, 0x3D, 0x08, 0x09,
    0x71, 0x77, 0x65, 0x72, 0x74, 0x79, 0x75, 0x69, 0x6F, 0x70, 0x5B, 0x5D, 0x0D, 0x00, 0x61, 0x73,
    0x64, 0x66, 0x67, 0x68, 0x6A, 0x6B, 0x6C, 0x3B, 0x27, 0x60, 0x00, 0x5C, 0x7A, 0x78, 0x63, 0x76,
    0x62, 0x6E, 0x6D, 0x2C, 0x2E, 0x2F, 0x00, 0x2A, 0x00, 0x20, 0x00,
};
static const u8 bios_shifted[0x59] = {
    0x00, 0x1B, 0x21, 0x40, 0x23, 0x24, 0x25, 0x5E, 0x26, 0x2A, 0x28, 0x29, 0x5F, 0x2B, 0x08, 0x00,
    0x51, 0x57, 0x45, 0x52, 0x54, 0x59, 0x55, 0x49, 0x4F, 0x50, 0x7B, 0x7D, 0x0D, 0x00, 0x41, 0x53,
    0x44, 0x46, 0x47, 0x48, 0x4A, 0x4B, 0x4C, 0x3A, 0x22, 0x7E, 0x00, 0x7C, 0x5A, 0x58, 0x43, 0x56,
    0x42, 0x4E, 0x4D, 0x3C, 0x3E, 0x3F, 0x00, 0x00, 0x00, 0x20, 0x00,
};

static void bios_int9(u8 raw)
{
    u8 sc = raw & 0x7F;
    bool brk = raw & 0x80;
    u8 flags = rd8(BDA, SHIFT_FLAGS);
    u8 bit = sc == 0x36 ? 0x01 : sc == 0x2A ? 0x02 : sc == 0x1D ? 0x04 : sc == 0x38 ? 0x08 : 0;
    if (bit) { wr8(BDA, SHIFT_FLAGS, brk ? flags & (u8)~bit : flags | bit); return; }
    if (brk || sc >= 0x59) return;
    if (sc == 0x3A) { wr8(BDA, SHIFT_FLAGS, flags ^ 0x40); return; }
    u8 v = (flags & 3) ? bios_shifted[sc] : bios_ascii[sc];
    if ((flags & 0x40) && v >= 'a' && v <= 'z' && !(flags & 3)) v = (u8)(v - 0x20);
    if ((flags & 4) && v >= 0x40) v &= 0x1F;
    u16 head = rd16(BDA, 0x1A), tail = rd16(BDA, 0x1C);
    u16 next = (u16)(tail + 2);
    if (next == rd16(BDA, 0x82)) next = rd16(BDA, 0x80);
    if (next == head) return;                             /* full: the BIOS beeps */
    wr16(BDA, tail, (u16)(sc << 8 | v));
    wr16(BDA, 0x1C, next);
}

bool bios_key_available(void)
{
    host_pump();
    return rd16(BDA, 0x1A) != rd16(BDA, 0x1C);
}

u16 bios_key_get(void)
{
    while (!bios_key_available()) { }
    return key_read();
}

/* ------------------------------------------------------------------ host glue */

void kbd_host_byte(u8 raw)
{
    if (int9_hooked && prog_layout) kbd_isr(raw);
    else bios_int9(raw);
}

void kbd_host_focus_lost(void)
{
    /* Keys released outside the window never send their break: release every held key. */
    if (!int9_hooked || !prog_layout) return;
    for (int sc = 1; sc < 0x59; sc++)
        if (LP(G_key_state)[sc] & 1) kbd_isr((u8)(sc | 0x80));
}

void kbd_unhook(void) { int9_hooked = false; }
