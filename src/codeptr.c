#include "codeptr.h"

#include "host.h"
#include "prog.h"

#define CODEPTR_MAX 2048
#define ABS_MAX 128

static struct { u8 prog; u32 addr; CodeFn fn; } table[CODEPTR_MAX];
static int count;
static struct { u32 key; CodeFn fn; } abs_table[ABS_MAX];
static int abs_count;

/* AP_flat.exe: the resident VROOMM stub segments (RE_GUIDE.md, "AP.EXE overlays"). */
#define AP_STUB_FIRST 0x3C59
#define AP_STUB_LAST  0x3EA0

void codeptr_register(int prog, u32 fn_addr, CodeFn fn)
{
    for (int i = 0; i < count; i++) {
        if (table[i].prog == prog && table[i].addr == fn_addr) { table[i].fn = fn; return; }
    }
    if (count == CODEPTR_MAX) host_fatal("codeptr table full");
    table[count].prog = (u8)prog;
    table[count].addr = fn_addr;
    table[count].fn = fn;
    count++;
}

void codeptr_register_shared(int f_id, CodeFn fn)
{
    for (int p = PROG_INTRO; p <= PROG_AP; p++) {
        u32 a = prog_desc(p)->layout->fn[f_id];
        if (a != LAYOUT_ABSENT_FN) codeptr_register(p, a, fn);
    }
}

void codeptr_register_abs(FarPtr p, CodeFn fn)
{
    u32 key = (u32)p.seg << 16 | p.off;
    for (int i = 0; i < abs_count; i++) {
        if (abs_table[i].key == key) { abs_table[i].fn = fn; return; }
    }
    if (abs_count == ABS_MAX) host_fatal("codeptr abs table full");
    abs_table[abs_count].key = key;
    abs_table[abs_count].fn = fn;
    abs_count++;
}

void codeptr_clear_abs(void) { abs_count = 0; }

FarPtr codeptr_far(u32 fn_addr)
{
    return far_make(SEG((u16)(fn_addr >> 16)), (u16)fn_addr);
}

FarPtr codeptr_far_shared(int f_id)
{
    u32 a = prog_layout->fn[f_id];
    if (a == LAYOUT_ABSENT_FN) host_fatal("function %s is not part of %s", layout_f_names[f_id], prog_layout->exe);
    return codeptr_far(a);
}

static CodeFn find(FarPtr p)
{
    for (int i = 0; i < abs_count; i++)
        if (abs_table[i].key == ((u32)p.seg << 16 | p.off)) return abs_table[i].fn;
    int id = prog_current_id();
    u16 fseg = (u16)(p.seg - LOAD_SEG);
    /* AP: a stub entry holds JMP FAR off:seg (flattened image) - follow it to the overlay code. */
    if (id == PROG_AP && fseg >= AP_STUB_FIRST && fseg <= AP_STUB_LAST && code_rd8(p.seg, p.off) == 0xEA) {
        u16 off = code_rd16(p.seg, (u16)(p.off + 1)), seg = code_rd16(p.seg, (u16)(p.off + 3));
        p = far_make(seg, off);
        fseg = (u16)(p.seg - LOAD_SEG);
    }
    u32 addr = (u32)fseg << 16 | p.off;
    for (int i = 0; i < count; i++)
        if (table[i].prog == id && table[i].addr == addr) return table[i].fn;
    return NULL;
}

bool codeptr_known(FarPtr p) { return far_is_null(p) || find(p) != NULL; }

CodeFn codeptr_lookup(FarPtr p)
{
    if (far_is_null(p)) return NULL;
    CodeFn f = find(p);
    if (!f)   /* ENH (diagnostic): the C caller, as an offset from codeptr_lookup (map it with nm) */
        host_fatal("%s: call through unregistered code pointer %04X:%04X (file %04X:%04X), caller codeptr_lookup%+ld",
                   prog_layout ? prog_layout->exe : "?", p.seg, p.off, (u16)(p.seg - LOAD_SEG), p.off,
                   (long)((const char *)__builtin_return_address(0) - (const char *)(void *)codeptr_lookup));
    return f;
}

CodeFn codeptr_lookup_near(u16 file_seg, u16 off)
{
    return codeptr_lookup(far_make(SEG(file_seg), off));
}
