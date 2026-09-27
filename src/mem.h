#pragma once
/* Real-mode memory model (platform.md §2, §5, §9; PORTING.md "Memory model").
 *
 * mem[] is the 1 MB real-mode address space (plus the HMA margin). Only one of the three programs is
 * resident at a time, as under GCD.COM: prog_run() (prog.c) loads INTRO.EXE, PS.EXE or AP.EXE from the
 * game folder at LOAD_SEG, applies the MZ relocations (AP: after flattening the VROOMM overlays exactly
 * like tools/unovl.py), and re-initialises DGROUP like DOS + Borland c0.
 *
 *   0000:0000  interrupt vectors (INT 67h points at the EMM device header, see ems.c)
 *   0040:0000  BIOS data area: 40:17 shift flags and the 40:1E..3D type-ahead buffer used by the
 *              INTRO/PS keyboard ISR, 40:63 CRTC port
 *   0000:0510  the GCD hand-off marker (29Ah / 3E7h), 0000:0512 the free-memory word (platform.md §2.2)
 *   ENV_SEG    environment block, PSP_SEG program segment prefix (command tail at PSP:0080)
 *   LOAD_SEG   the program image: file address SSSS:OOOO = segment SEG(0xSSSS), offset OOOO
 *   DGROUP     SEG(dgroup file segment): a full 64 KB segment (static data, BSS, near heap, stack;
 *              heaplen 0 => c0 gives DGROUP the whole 64 KB and SP = 0, i.e. the stack starts at the top)
 *   above      DOS memory arena up to 9FFF:000F (dos_mem_alloc: INT 21h 48h/49h/4Ah with MCBs)
 *   A000-AFFF  NOT used: video memory is the VGA model's own 256 KB (platform/vga.h, vga_ptr())
 *   E000:0000  the EMS page frame (64 KB, 4 physical pages, ems.h)
 *   F000:FFFE  BIOS model byte (FCh, AT: is_tandy() is false)
 *
 * AP: the flattened overlay code (work/AP_flat.exe segments 45CC-728E) would overlap AP's own DGROUP
 * (3EA4..4EA3: the 64 KB DGROUP extends beyond the resident image, the overlays were placed right
 * after it by unovl.py). Overlay code is never written (VROOMM may discard and reload it), so it is kept
 * in a separate read-only store: code_rd8/16() read any code address, including overlay segments.
 * Code addresses therefore stay equal to AP_flat.exe addresses + LOAD_SEG. */
#include "types.h"
#include "symbols_layout.h"

#define MEM_SIZE    0x110000u
#define LOAD_SEG    0x0200                       /* program image (PSP at LOAD_SEG - 10h) */
#define PSP_SEG     (LOAD_SEG - 0x10)
#define ENV_SEG     (LOAD_SEG - 0x30)            /* environment block, 1Fh paragraphs */
#define ENV_PARAS   0x1F
#define SEG(file_seg) ((u16)(LOAD_SEG + (file_seg)))   /* segment of a file address SSSS:OOOO */
#define MEM_TOP_SEG 0xA000                       /* end of conventional memory (PSP:0002) */
#define EMS_FRAME_SEG 0xE000
#define AP_OVL_FILE_SEG 0x45CC                   /* first overlay code segment in AP_flat.exe */

extern u8 mem[MEM_SIZE];

/* Current program (prog.c): its DGROUP segment (relocated) and its shared-library layout. */
extern u16 cur_dgroup;
extern const ProgLayout *prog_layout;

static inline u32 lin(u16 seg, u16 off) { return ((u32)seg << 4) + off; }
static inline u8 *mp(u16 seg, u16 off) { return mem + lin(seg, off); }

static inline u8  rd8 (u16 seg, u16 off) { return mem[lin(seg, off)]; }
static inline u16 rd16(u16 seg, u16 off) { u8 *p = mp(seg, off); return (u16)(p[0] | p[1] << 8); }
static inline u32 rd32(u16 seg, u16 off) { u8 *p = mp(seg, off); return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static inline void wr8 (u16 seg, u16 off, u8 v)  { mem[lin(seg, off)] = v; }
static inline void wr16(u16 seg, u16 off, u16 v) { u8 *p = mp(seg, off); p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static inline void wr32(u16 seg, u16 off, u32 v) { u8 *p = mp(seg, off); p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

/* Lvalue accessors for globals of the CURRENT program's DGROUP (little-endian host). Program-specific
 * code uses its own symbols: DSW(PS_DS_quit_flag) = 1. Code-segment variables: SEGW(file_seg, off). */
#define DSB(o)  (*(u8  *)mp(cur_dgroup, (u16)(o)))
#define DSC(o)  (*(s8  *)mp(cur_dgroup, (u16)(o)))
#define DSW(o)  (*(u16 *)mp(cur_dgroup, (u16)(o)))
#define DSS(o)  (*(s16 *)mp(cur_dgroup, (u16)(o)))
#define DSL(o)  (*(u32 *)mp(cur_dgroup, (u16)(o)))
#define DSSL(o) (*(s32 *)mp(cur_dgroup, (u16)(o)))
#define DSP(o)  (mp(cur_dgroup, (u16)(o)))                  /* u8 * to DS:o */
#define SEGB(s, o) (*(u8  *)mp(SEG(s), (u16)(o)))
#define SEGC(s, o) (*(s8  *)mp(SEG(s), (u16)(o)))
#define SEGW(s, o) (*(u16 *)mp(SEG(s), (u16)(o)))
#define SEGS(s, o) (*(s16 *)mp(SEG(s), (u16)(o)))

/* Shared-library globals through the current program's layout (symbols_layout.h, PORTING.md):
 *     LW(G_timer_countdown) = 0;   u8 *ks = LP(G_key_state);
 * A global the current program does not have is a fatal error (layout_absent). */
_Noreturn void layout_absent(int g);
static inline u16 lds(int g)
{
    u16 o = prog_layout->ds[g];
    if (o == LAYOUT_ABSENT_DS) layout_absent(g);
    return o;
}
#define LB(g)  DSB(lds(g))
#define LC(g)  DSC(lds(g))
#define LW(g)  DSW(lds(g))
#define LS(g)  DSS(lds(g))
#define LL(g)  DSL(lds(g))
#define LSL(g) DSSL(lds(g))
#define LP(g)  DSP(lds(g))

/* 16:16 far pointer as stored in memory (offset first). */
typedef struct { u16 off, seg; } FarPtr;

static inline FarPtr far_rd(u16 seg, u16 off) { FarPtr p = { rd16(seg, off), rd16(seg, (u16)(off + 2)) }; return p; }
static inline void   far_wr(u16 seg, u16 off, FarPtr p) { wr16(seg, off, p.off); wr16(seg, (u16)(off + 2), p.seg); }
static inline FarPtr ds_far(u16 off) { return far_rd(cur_dgroup, off); }             /* far ptr stored at DS:off */
static inline void   ds_far_wr(u16 off, FarPtr p) { far_wr(cur_dgroup, off, p); }
static inline u8    *far_mp(FarPtr p) { return mp(p.seg, p.off); }
static inline FarPtr far_make(u16 seg, u16 off) { FarPtr p = { off, seg }; return p; }
static inline FarPtr far_add(FarPtr p, u16 n) { p.off = (u16)(p.off + n); return p; }   /* far (not huge) arithmetic */
static inline u32    far_lin(FarPtr p) { return lin(p.seg, p.off); }
static inline FarPtr far_norm(u32 linear) { FarPtr p = { (u16)(linear & 0x0F), (u16)(linear >> 4) }; return p; }
static inline bool   far_is_null(FarPtr p) { return p.off == 0 && p.seg == 0; }
static inline FarPtr ds_ptr(u16 off) { return far_make(cur_dgroup, off); }          /* DGROUP address as far ptr */

/* Code bytes (switch tables, CS data, the AP stub thunks) at a relocated code address; AP overlay
 * segments come from the read-only overlay store (see the header comment). */
u8  code_rd8(u16 seg, u16 off);
u16 code_rd16(u16 seg, u16 off);

/* ---- The emulated DGROUP stack (SS = DS). Locals whose near address the original passes on (sprintf
 * buffers, structs handed to library calls) are allocated here so they have a DS offset:
 *     u16 mark = stk_mark(); u16 buf = stk_alloc(80); ... stk_release(mark);
 * dg_sp starts below the argv strings at the top of DGROUP (c0: SP = 0 with heaplen 0). */
extern u16 dg_sp;
u16  stk_alloc(u16 bytes);                        /* returns the DS offset (word aligned) */
static inline u16 stk_mark(void) { return dg_sp; }
static inline void stk_release(u16 mark) { dg_sp = mark; }

/* ---- Division as the original CPU + the Dynamix INT 0 handler perform it (platform.md §3.6).
 * divzero_isr retries an overflowing 16-bit DIV/IDIV with the dividend halved (arithmetic shift of
 * DX:AX, or AX for the byte forms) until the quotient fits. A true division by zero (and an unsigned
 * DIV whose dividend has bit 31 set) hung the original: unreachable, fatal in debug builds, 0 in
 * release builds. 386 (66h-prefixed) divisions must never overflow: the handler corrupted IP. */
u16 div_unreachable(const char *what);
static inline s16 idiv16_retry(s32 num, s16 den, s16 *rem)
{
    if (den == 0) return (s16)div_unreachable("IDIV by zero");
    s32 q;
    while ((q = num / den) > 32767 || q < -32768) num >>= 1;   /* sar dx,1 / rcr ax,1 */
    if (rem) *rem = (s16)(num % den);
    return (s16)q;
}
static inline u16 div16_retry(u32 num, u16 den, u16 *rem)
{
    if (den == 0) return div_unreachable("DIV by zero");
    while (num / den > 0xFFFF) {
        if (num & 0x80000000u) return div_unreachable("DIV overflow with bit 31 set");
        num >>= 1;                                               /* sar keeps bit 31 = 0 */
    }
    if (rem) *rem = (u16)(num % den);
    return (u16)(num / den);
}
static inline s8 idiv8_retry(s16 num, s8 den, s8 *rem)
{
    if (den == 0) return (s8)div_unreachable("IDIV8 by zero");
    int q;
    while ((q = num / den) > 127 || q < -128) num >>= 1;        /* sar ax,1 */
    if (rem) *rem = (s8)(num % den);
    return (s8)q;
}
static inline u8 div8_retry(u16 num, u8 den, u8 *rem)
{
    if (den == 0) return (u8)div_unreachable("DIV8 by zero");
    while (num / den > 0xFF) {
        if (num & 0x8000u) return (u8)div_unreachable("DIV8 overflow with bit 15 set");
        num >>= 1;
    }
    if (rem) *rem = (u8)(num % den);
    return (u8)(num / den);
}
static inline s32 idiv32_386(int64_t num, s32 den, s32 *rem)     /* 66h IDIV: must not overflow */
{
    if (den == 0) return (s32)div_unreachable("IDIV32 by zero");
    int64_t q = num / den;
    if (q > 0x7FFFFFFF || q < -0x7FFFFFFF - 1) return (s32)div_unreachable("IDIV32 overflow");
    if (rem) *rem = (s32)(num % den);
    return (s32)q;
}
static inline u32 div32_386(uint64_t num, u32 den, u32 *rem)     /* 66h DIV: must not overflow */
{
    if (den == 0) return div_unreachable("DIV32 by zero");
    if (num / den > 0xFFFFFFFFu) return div_unreachable("DIV32 overflow");
    if (rem) *rem = (u32)(num % den);
    return (u32)(num / den);
}

/* ---- Loading (mem.c). Reads an EXE, flattens AP's overlays, relocates it to LOAD_SEG. */
typedef struct {
    u8  *image;          /* flattened load image (not relocated) */
    u32  image_len;
    u8  *reloc_map;      /* 1 byte per image byte: 1 = a relocated word starts here */
    u32  nrelocs;
    u16  mz_ss, mz_sp, mz_cs, mz_ip;
    u32  ovl_start;      /* AP: image offset of the first overlay segment (45CC0h), else image_len */
    int  ovl_segments, ovl_entries, ovl_bad_relocs;
} ExeImage;

bool mem_read_exe(const char *path, bool flatten_vroomm, ExeImage *out, char *err, size_t errlen);
void mem_free_exe(ExeImage *img);
/* Clears the program area, copies the image to LOAD_SEG and applies the relocations; the part at and
 * above img->ovl_start goes to the read-only overlay store. */
void mem_place_image(const ExeImage *img);

/* ---- Low memory and the DOS memory arena (INT 21h 48h/49h/4Ah, platform.md §5.1). */
void mem_init_machine(void);                          /* once: IVT, BIOS data area, BIOS model byte */
void dos_arena_reset(u16 psp_seg, u16 prog_end_seg);  /* env + program blocks, the rest free */
bool dos_mem_alloc(u16 paras, u16 *seg_or_largest);   /* AH=48h: false = CF (value = largest block) */
bool dos_mem_free(u16 seg);                           /* AH=49h */
bool dos_mem_resize(u16 seg, u16 paras, u16 *largest);/* AH=4Ah */
u16  dos_mem_largest(void);                           /* AH=48h BX=FFFFh result */

/* Interrupt vector table entries (INT 21h 25h/35h). */
static inline FarPtr ivt_get(u8 n) { return far_rd(0, (u16)(n * 4)); }
static inline void   ivt_set(u8 n, FarPtr p) { far_wr(0, (u16)(n * 4), p); }
