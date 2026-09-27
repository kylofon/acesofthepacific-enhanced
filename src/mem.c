/* Real-mode memory: loader (MZ + VROOMM flattening), low memory, DOS arena, helpers. See mem.h. */
#include "mem.h"
#include "host.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

u8 mem[MEM_SIZE];
u16 dg_sp;

/* AP's flattened overlay code (read-only, see mem.h): linear range [ovl_lin, ovl_lin + ovl_len). */
static u8 *ovl_store;
static u32 ovl_lin, ovl_len;

_Noreturn void layout_absent(int g)
{
    host_fatal("shared global %s is not part of %s (symbols_layout.h)",
               (g >= 0 && g < G_COUNT) ? layout_g_names[g] : "?", prog_layout ? prog_layout->exe : "?");
}

u16 div_unreachable(const char *what)
{
#ifndef NDEBUG
    host_fatal("%s: the original hangs here (INT 0 handler, platform.md 3.6)", what);
#else
    (void)what;
    return 0;
#endif
}

u8 code_rd8(u16 seg, u16 off)
{
    u32 a = lin(seg, off);
    if (ovl_store && seg >= SEG(AP_OVL_FILE_SEG) && a >= ovl_lin && a < ovl_lin + ovl_len)
        return ovl_store[a - ovl_lin];
    return mem[a];
}

u16 code_rd16(u16 seg, u16 off)
{
    return (u16)(code_rd8(seg, off) | code_rd8(seg, (u16)(off + 1)) << 8);
}

u16 stk_alloc(u16 bytes)
{
    u16 n = (u16)((bytes + 1) & ~1u);
    if ((u32)(dg_sp ? dg_sp : 0x10000u) < (u32)n + 0x100) host_fatal("DGROUP stack model overflow");
    dg_sp = (u16)(dg_sp - n);
    return dg_sp;
}

/* ------------------------------------------------------------------ loader */

static void set_err(char *err, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

static u16 le16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 le32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static void put16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

typedef struct {
    u16 stub, seg;            /* stub segment (resident) and the placed code segment */
    u32 fileoff;              /* FBOV-relative offset of the code */
    u16 size, relsize, nent;
    u16 *entries;             /* entry offsets in the overlay code */
} Ovl;

/* Flatten the Borland VROOMM overlays exactly like tools/unovl.py (default --direct mode): place every
 * overlay segment after the resident image (paragraph aligned), turn its relocations (segment-table
 * index * 8) into ordinary relocations of the real segment, rewrite each stub entry CD 3F off 00 into
 * EA off seg (JMP FAR), and retarget `call far stub:entry` and `mov r16,entry / mov r16,stub` pairs
 * straight to the overlay code. Result = work/AP_flat.exe's load image and relocation set. */
static bool flatten_vroomm(const u8 *d, size_t dlen, size_t load_end, ExeImage *x, char *err, size_t errlen)
{
    const u8 *img = x->image;
    size_t fb = load_end;
    if (fb + 16 > dlen || memcmp(d + fb, "FBOV", 4) != 0) { set_err(err, errlen, "no FBOV overlay block"); return false; }
    u32 ov_size = le32(d + fb + 4), segtab = le32(d + fb + 8), nsegs = le32(d + fb + 12);
    size_t OV = fb + 16;
    if (OV + ov_size != dlen || segtab + 8ull * nsegs > dlen || nsegs == 0 || nsegs > 0x2000) {
        set_err(err, errlen, "bad FBOV header");
        return false;
    }
    Ovl *ovls = SDL_calloc(nsegs, sizeof *ovls);
    s16 *stub_idx = SDL_malloc(0x10000 * sizeof(s16));
    bool ok = false;
    u8 *nimg = NULL, *nrel = NULL;
    if (!ovls || !stub_idx) { set_err(err, errlen, "out of memory"); goto done; }
    for (u32 i = 0; i < 0x10000; i++) stub_idx[i] = -1;

    int n = 0;
    for (u32 i = 0; i < nsegs; i++) {
        const u8 *e = d + segtab + 8 * i;
        u16 seg = le16(e), flags = le16(e + 4);
        if (flags != 3) continue;
        size_t b = (size_t)seg * 16;
        if (b + 0x20 > x->image_len || img[b] != 0xCD || img[b + 1] != 0x3F) { set_err(err, errlen, "bad stub %04X", seg); goto done; }
        Ovl *o = &ovls[n];
        o->stub = seg;
        o->fileoff = le32(img + b + 4);
        o->size = le16(img + b + 8);
        o->relsize = le16(img + b + 10);
        o->nent = le16(img + b + 12);
        if (OV + o->fileoff + o->size + o->relsize > dlen || b + 0x20 + 5u * o->nent > x->image_len) {
            set_err(err, errlen, "overlay %04X out of range", seg);
            goto done;
        }
        o->entries = SDL_malloc(sizeof(u16) * (o->nent + 1u));
        if (!o->entries) { set_err(err, errlen, "out of memory"); goto done; }
        for (u16 k = 0; k < o->nent; k++) {
            const u8 *en = img + b + 0x20 + 5u * k;
            if (en[0] != 0xCD || en[1] != 0x3F || en[4] != 0) { set_err(err, errlen, "bad stub entry %04X:%04X", seg, 0x20 + 5 * k); goto done; }
            o->entries[k] = le16(en + 2);
        }
        stub_idx[seg] = (s16)n;
        n++;
    }

    /* place */
    u32 cur = (x->image_len + 15) / 16;
    for (int k = 0; k < n; k++) {
        ovls[k].seg = (u16)cur;
        cur += (ovls[k].size + 15u) / 16;
    }
    u32 nlen = cur * 16;
    nimg = SDL_calloc(1, nlen);
    nrel = SDL_calloc(1, nlen);
    if (!nimg || !nrel) { set_err(err, errlen, "out of memory"); goto done; }
    memcpy(nimg, img, x->image_len);
    memcpy(nrel, x->reloc_map, x->image_len);

    for (int k = 0; k < n; k++) {
        Ovl *o = &ovls[k];
        u32 base = (u32)o->seg * 16;
        u8 *code = nimg + base;
        memcpy(code, d + OV + o->fileoff, o->size);
        const u8 *rel = d + OV + o->fileoff + o->size;
        for (u16 r = 0; r < o->relsize / 2; r++) {
            u16 at = le16(rel + 2 * r);
            if ((u32)at + 2 > o->size) { x->ovl_bad_relocs++; continue; }
            u16 v = le16(code + at);
            if (v % 8 || v / 8 >= nsegs) { x->ovl_bad_relocs++; continue; }
            put16(code + at, le16(d + segtab + v));          /* entry v/8: its segment (stub for overlays) */
            nrel[base + at] = 1;
        }
        for (u16 e = 0; e < o->nent; e++) {                  /* stub entries -> JMP FAR off:seg */
            u8 *p = nimg + (u32)o->stub * 16 + 0x20 + 5u * e;
            p[0] = 0xEA;
            put16(p + 1, o->entries[e]);
            put16(p + 3, o->seg);
            nrel[(u32)o->stub * 16 + 0x20 + 5u * e + 3] = 1;
        }
        x->ovl_entries += o->nent;
    }

    /* direct calls / code pointers (unovl.py --direct, the default) */
    for (u32 p = 3; p + 2 <= nlen; p++) {
        if (!nrel[p]) continue;
        u16 sv = le16(nimg + p);
        if (stub_idx[sv] < 0) continue;
        const Ovl *o = &ovls[stub_idx[sv]];
        u16 off;
        bool call = nimg[p - 3] == 0x9A;
        bool movs = !call && p >= 4 && nimg[p - 1] >= 0xB8 && nimg[p - 1] <= 0xBF && nimg[p - 4] >= 0xB8 && nimg[p - 4] <= 0xBF;
        if (!call && !movs) continue;
        off = call ? le16(nimg + p - 2) : le16(nimg + p - 3);
        if (off < 0x20 || (off - 0x20) % 5 || (off - 0x20) / 5 >= o->nent) continue;
        u16 t_off = o->entries[(off - 0x20) / 5];
        if (call) {
            put16(nimg + p - 2, t_off);
            put16(nimg + p, o->seg);
        } else {
            put16(nimg + p - 3, t_off);
            put16(nimg + p, o->seg);
        }
    }

    x->ovl_start = x->image_len;
    x->ovl_segments = n;
    SDL_free(x->image);
    SDL_free(x->reloc_map);
    x->image = nimg;
    x->reloc_map = nrel;
    x->image_len = nlen;
    nimg = nrel = NULL;
    x->nrelocs = 0;
    for (u32 p = 0; p < nlen; p++) x->nrelocs += x->reloc_map[p];
    ok = true;
done:
    if (ovls) for (u32 i = 0; i < nsegs; i++) SDL_free(ovls[i].entries);
    SDL_free(ovls);
    SDL_free(stub_idx);
    SDL_free(nimg);
    SDL_free(nrel);
    return ok;
}

bool mem_read_exe(const char *path, bool flatten, ExeImage *x, char *err, size_t errlen)
{
    memset(x, 0, sizeof *x);
    size_t dlen;
    u8 *d = SDL_LoadFile(path, &dlen);
    if (!d) { set_err(err, errlen, "cannot read %s: %s", path, SDL_GetError()); return false; }
    bool ok = false;
    if (dlen < 0x1C || le16(d) != 0x5A4D) { set_err(err, errlen, "%s is not a DOS executable", path); goto done; }
    u16 cblp = le16(d + 2), cp = le16(d + 4), crlc = le16(d + 6), cparhdr = le16(d + 8);
    size_t load_end = (size_t)(cp - 1) * 512 + (cblp ? cblp : 512);
    size_t hdr = (size_t)cparhdr * 16;
    u16 lfarlc = le16(d + 0x18);
    if (load_end > dlen || hdr >= load_end || lfarlc + 4ull * crlc > dlen) { set_err(err, errlen, "%s: bad MZ header", path); goto done; }
    x->mz_ss = le16(d + 0x0E); x->mz_sp = le16(d + 0x10); x->mz_ip = le16(d + 0x14); x->mz_cs = le16(d + 0x16);
    x->image_len = (u32)(load_end - hdr);
    x->image = SDL_malloc(x->image_len);
    x->reloc_map = SDL_calloc(1, x->image_len);
    if (!x->image || !x->reloc_map) { set_err(err, errlen, "out of memory"); goto done; }
    memcpy(x->image, d + hdr, x->image_len);
    for (u16 i = 0; i < crlc; i++) {
        u32 at = (u32)le16(d + lfarlc + 4 * i + 2) * 16 + le16(d + lfarlc + 4 * i);
        if (at + 2 <= x->image_len) x->reloc_map[at] = 1;
    }
    x->ovl_start = x->image_len;
    if (flatten) {
        if (!flatten_vroomm(d, dlen, load_end, x, err, errlen)) goto done;
    } else {
        for (u32 p = 0; p < x->image_len; p++) x->nrelocs += x->reloc_map[p];
    }
    ok = true;
done:
    SDL_free(d);
    if (!ok) mem_free_exe(x);
    return ok;
}

void mem_free_exe(ExeImage *x)
{
    SDL_free(x->image);
    SDL_free(x->reloc_map);
    x->image = x->reloc_map = NULL;
}

void mem_place_image(const ExeImage *x)
{
    /* DOS does not clear freed memory; the port clears the program area for determinism. */
    memset(mp(ENV_SEG, 0), 0, lin(MEM_TOP_SEG, 0) - lin(ENV_SEG, 0));
    SDL_free(ovl_store);
    ovl_store = NULL;
    ovl_len = 0;

    u8 *buf = SDL_malloc(x->image_len);
    if (!buf) host_fatal("out of memory");
    memcpy(buf, x->image, x->image_len);
    for (u32 p = 0; p + 1 < x->image_len; p++) {
        if (!x->reloc_map[p]) continue;
        put16(buf + p, (u16)(le16(buf + p) + LOAD_SEG));
    }
    u32 base = lin(LOAD_SEG, 0);
    u32 resident = x->ovl_start;
    if (base + resident > lin(MEM_TOP_SEG, 0)) host_fatal("load image too large");
    memcpy(mem + base, buf, resident);
    if (x->image_len > resident) {
        ovl_len = x->image_len - resident;
        ovl_store = SDL_malloc(ovl_len);
        if (!ovl_store) host_fatal("out of memory");
        memcpy(ovl_store, buf + resident, ovl_len);
        ovl_lin = base + resident;
    }
    SDL_free(buf);
}

/* ------------------------------------------------------------------ low memory */

#define BIOS_IRET_OFF 0xFF53                 /* F000:FF53 = IRET, the default vector target */

void mem_init_machine(void)
{
    memset(mem, 0, sizeof mem);
    wr8(0xF000, BIOS_IRET_OFF, 0xCF);
    for (int n = 0; n < 256; n++) ivt_set((u8)n, far_make(0xF000, BIOS_IRET_OFF));
    wr8(0xF000, 0xFFFE, 0xFC);               /* AT model byte: is_tandy() = 0 */
    wr16(0x40, 0x10, 0x0021);                /* equipment: 80x25 colour, FPU none, 1 floppy flag */
    wr16(0x40, 0x13, 640);                   /* conventional memory in KB */
    wr8 (0x40, 0x17, 0x00);                  /* shift flags (NumLock off) */
    wr16(0x40, 0x1A, 0x1E);                  /* type-ahead buffer head/tail: empty */
    wr16(0x40, 0x1C, 0x1E);
    wr8 (0x40, 0x49, 0x03);                  /* text mode 3 before the first program */
    wr16(0x40, 0x4A, 80);
    wr16(0x40, 0x63, 0x3D4);                 /* CRTC port (drv_flip reads it) */
    wr16(0x40, 0x80, 0x1E);                  /* buffer start/end */
    wr16(0x40, 0x82, 0x3E);
}

/* ------------------------------------------------------------------ DOS memory arena */

#define MCB_FIRST ((u16)(ENV_SEG - 1))

typedef struct { u8 type; u16 owner, size; } Mcb;

static Mcb mcb_get(u16 m) { Mcb c = { rd8(m, 0), rd16(m, 1), rd16(m, 3) }; return c; }
static void mcb_set(u16 m, u8 type, u16 owner, u16 size) { wr8(m, 0, type); wr16(m, 1, owner); wr16(m, 3, size); }

static void mcb_check(u16 m, Mcb c)
{
    if (c.type != 'M' && c.type != 'Z') host_fatal("DOS memory arena corrupt at %04X", m);
}

void dos_arena_reset(u16 psp_seg, u16 prog_end_seg)
{
    mcb_set(MCB_FIRST, 'M', psp_seg, ENV_PARAS);
    memcpy(mp(MCB_FIRST, 8), "ENV\0\0\0\0\0", 8);
    mcb_set((u16)(psp_seg - 1), 'M', psp_seg, (u16)(prog_end_seg - psp_seg));
    mcb_set(prog_end_seg, 'Z', 0, (u16)(MEM_TOP_SEG - prog_end_seg - 1));
}

/* DOS 5 first fit: free blocks are merged with following free blocks while the chain is walked. */
static void merge_free(u16 m)
{
    Mcb c = mcb_get(m);
    while (c.type == 'M' && c.owner == 0) {
        u16 nx = (u16)(m + 1 + c.size);
        Mcb n = mcb_get(nx);
        mcb_check(nx, n);
        if (n.owner != 0) break;
        c.size = (u16)(c.size + 1 + n.size);
        c.type = n.type;
        mcb_set(m, c.type, 0, c.size);
    }
}

bool dos_mem_alloc(u16 paras, u16 *seg_or_largest)
{
    u16 largest = 0;
    for (u16 m = MCB_FIRST;;) {
        Mcb c = mcb_get(m);
        mcb_check(m, c);
        if (c.owner == 0) {
            merge_free(m);
            c = mcb_get(m);
            if (c.size >= paras) {
                if (c.size > paras) {
                    mcb_set((u16)(m + 1 + paras), c.type, 0, (u16)(c.size - paras - 1));
                    c.type = 'M';
                }
                mcb_set(m, c.type, PSP_SEG, paras);
                *seg_or_largest = (u16)(m + 1);
                return true;
            }
            if (c.size > largest) largest = c.size;
        }
        if (c.type == 'Z') break;
        m = (u16)(m + 1 + c.size);
    }
    *seg_or_largest = largest;
    return false;
}

bool dos_mem_free(u16 seg)
{
    u16 m = (u16)(seg - 1);
    Mcb c = mcb_get(m);
    if (c.type != 'M' && c.type != 'Z') return false;      /* error 9: invalid block */
    mcb_set(m, c.type, 0, c.size);
    return true;
}

bool dos_mem_resize(u16 seg, u16 paras, u16 *largest)
{
    u16 m = (u16)(seg - 1);
    Mcb c = mcb_get(m);
    if (c.type != 'M' && c.type != 'Z') { if (largest) *largest = 0; return false; }
    /* grow into following free blocks */
    u16 avail = c.size;
    if (c.type == 'M') {
        u16 nx = (u16)(m + 1 + c.size);
        merge_free(nx);
        Mcb n = mcb_get(nx);
        if (n.owner == 0) avail = (u16)(c.size + 1 + n.size);
    }
    if (paras > avail) { if (largest) *largest = avail; return false; }
    u8 type = c.type;
    if (avail != c.size) {                                  /* absorb the free neighbour */
        u16 nx = (u16)(m + 1 + c.size);
        type = mcb_get(nx).type;
    }
    if (paras < avail) {
        mcb_set((u16)(m + 1 + paras), type, 0, (u16)(avail - paras - 1));
        type = 'M';
    }
    mcb_set(m, type, c.owner, paras);
    return true;
}

u16 dos_mem_largest(void)
{
    u16 v;
    if (dos_mem_alloc(0xFFFF, &v)) host_fatal("DOS arena: 0FFFFh paragraphs allocated?");
    return v;
}
