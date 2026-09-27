/* VGA hardware model, see vga.h. */
#include "platform/vga.h"

#include <string.h>

#include "host.h"
#include "enhanced/enhanced.h"   /* ENH */

static u8 vram[VGA_VRAM_SIZE];
static u8 dac[256][3];
static u8 mode = 0x03;
static u8 crtc_index, crtc[0x19];
static u8 seq_index, seq[5];
static u8 gc_index, gc[9];
static u8 attr_index, attr[0x15];
static bool attr_flipflop;                    /* false = next 3C0h write is an index */
static u8 dac_windex, dac_wsub, dac_rindex, dac_rsub, dac_state;
static u8 dac_wbuf[3];

/* Composition cache: report "changed" only when the picture really changed. */
static u32 last_frame[320 * 200];
static bool dirty = true;

/* 70.086 Hz: frame = 800 x 449 pixel clocks at 25.175 MHz; retrace = lines 412..413. */
#define FRAME_NUM (800ull * 449ull * 1000000000ull)
#define FRAME_DEN (25175000ull)
#define RETRACE_FIRST_LINE 412u
#define RETRACE_LINES 2u
#define LINES 449u

static u32 pal32(u8 i)
{
    u32 r = (u32)(dac[i][0] << 2 | dac[i][0] >> 4);
    u32 g = (u32)(dac[i][1] << 2 | dac[i][1] >> 4);
    u32 b = (u32)(dac[i][2] << 2 | dac[i][2] >> 4);
    return r << 16 | g << 8 | b;
}

static bool compose(u32 *xrgb)
{
    static u32 out[320 * 200];
    static u8 otag[320 * 200];                               /* ENH: the pixels' tags */
    u32 pal[256];
    for (int i = 0; i < 256; i++) pal[i] = pal32((u8)i);
    if (enh_vtag) memset(otag, ENH_TAG_2D, sizeof otag);
    if (mode != 0x13) {
        memset(out, 0, sizeof out);                          /* text mode: not modelled, black */
    } else {
        u16 start = (u16)(crtc[0x0C] << 8 | crtc[0x0D]);
        u16 pitch = (u16)(crtc[0x13] * 2);                   /* byte mode: offset register x 2 */
        int pan = (attr[0x13] & 7) >> 1;                     /* 256-colour mode: 2 pel units per pixel */
        for (int y = 0; y < 200; y++) {
            u16 row = (u16)(start + y * pitch);
            u32 *o = out + y * 320;
            for (int x = 0; x < 320; x++) {
                int c = x + pan;
                u16 a = (u16)(row + (c >> 2));
                o[x] = pal[vram[(u32)a * 4 + (c & 3)]];
                if (enh_vtag) otag[y * 320 + x] = enh_vtag[(u32)a * 4 + (c & 3)];   /* ENH */
            }
        }
    }
    bool changed = dirty || memcmp(out, last_frame, sizeof out) != 0;
    if (changed) {
        dirty = false;
        memcpy(last_frame, out, sizeof out);
    }
    /* ENH: the enhanced view under the scaled picture */
    if (enh_enabled()) return enh_compose(out, otag, pal, mode == 0x13 ? vga_shown_seg() : 0, changed, xrgb);
    if (!changed) return false;
    memcpy(xrgb, out, sizeof out);
    return true;
}

void vga_init(void)
{
    memset(vram, 0, sizeof vram);
    memset(dac, 0, sizeof dac);
    mode = 0x03;
    memset(crtc, 0, sizeof crtc);
    crtc[0x13] = 40;
    memset(attr, 0, sizeof attr);
    attr_flipflop = false;
    dirty = true;
    int w, h, x0, sw;                                      /* ENH: output size (320x200 with --classic) */
    enh_frame_size(&w, &h, &x0, &sw);
    host_set_frame_source(compose, w, h);
    host_set_screen_rect(x0, 0, sw, h);
    enh_vram_cleared();
}

u8 *vga_vram(void) { return vram; }

u8 *vga_page(u16 seg)
{
    u32 o = (u32)(u16)(seg - 0xA000) * 64u;                /* (seg - A000h) * 16 planar bytes * 4 */
    if (o >= VGA_VRAM_SIZE) host_fatal("vga_page(%04X): not video memory", seg);
    return vram + o;
}

void vga_latch_copy(u16 dst, u16 src, u16 n)
{
    for (u32 i = 0; i < n; i++)                            /* byte by byte: overlap behaves as the CPU copy */
        memcpy(vram + (u16)(dst + i) * 4u, vram + (u16)(src + i) * 4u, 4);
}

void vga_set_mode(u8 m)
{
    mode = m;
    memset(crtc, 0, sizeof crtc);
    crtc[0x13] = 40;
    memset(attr, 0, sizeof attr);
    attr_flipflop = false;
    if (m == 0x13) {
        memset(vram, 0, 0x10000);                          /* BIOS clears the chained 64000 bytes */
        for (int i = 0; i < 256; i++) dac[i][0] = dac[i][1] = dac[i][2] = 0;   /* PORT: default palette not modelled */
        enh_vram_cleared();                                /* ENH: tags */
    }
    dirty = true;
}

void vga_set_unchained(void)
{
    seq[4] = (u8)((seq[4] & ~0x08) | 0x04);
    gc[6] &= (u8)~0x02;
    memset(vram, 0, sizeof vram);                          /* SC[02] = 0Fh; clear A000:0000..FFFF */
    enh_vram_cleared();                                    /* ENH: tags */
    crtc[0x14] &= (u8)~0x40;
    crtc[0x17] |= 0x40;
    gc[5] = 0x40; gc[1] = 0; gc[8] = 0xFF;
    dirty = true;
}

u8 vga_mode(void) { return mode; }

void vga_dac_set(u8 i, u8 r, u8 g, u8 b)
{
    dac[i][0] = r & 0x3F;
    dac[i][1] = g & 0x3F;
    dac[i][2] = b & 0x3F;
}

void vga_dac_get(u8 i, u8 *r, u8 *g, u8 *b)
{
    if (r) *r = dac[i][0];
    if (g) *g = dac[i][1];
    if (b) *b = dac[i][2];
}

u16 vga_start(void) { return (u16)(crtc[0x0C] << 8 | crtc[0x0D]); }
u16 vga_shown_seg(void) { return (u16)(0xA000 + (vga_start() >> 4)); }

void vga_out(u16 port, u8 v)
{
    switch (port) {
    case 0x3C0:
        if (!attr_flipflop) attr_index = v;
        else if ((attr_index & 0x1F) < sizeof attr) attr[attr_index & 0x1F] = v;
        attr_flipflop = !attr_flipflop;
        break;
    case 0x3C4: seq_index = v; break;
    case 0x3C5: if (seq_index < sizeof seq) seq[seq_index] = v; break;
    case 0x3C7: dac_rindex = v; dac_rsub = 0; dac_state = 3; break;
    case 0x3C8: dac_windex = v; dac_wsub = 0; dac_state = 0; break;
    case 0x3C9:
        dac_wbuf[dac_wsub++] = v & 0x3F;
        if (dac_wsub == 3) {
            memcpy(dac[dac_windex], dac_wbuf, 3);
            dac_windex++;
            dac_wsub = 0;
        }
        break;
    case 0x3CE: gc_index = v; break;
    case 0x3CF: if (gc_index < sizeof gc) gc[gc_index] = v; break;
    case 0x3D4: crtc_index = v; break;
    case 0x3D5: if (crtc_index < sizeof crtc) crtc[crtc_index] = v; break;
    default: break;
    }
}

void vga_outw(u16 port, u16 v)
{
    vga_out(port, (u8)v);
    vga_out((u16)(port + 1), (u8)(v >> 8));
}

static u64 frame_phase_ns(u64 t, u64 *frame_start)
{
    u64 n = t * FRAME_DEN / FRAME_NUM;                     /* frames since start */
    *frame_start = n * FRAME_NUM / FRAME_DEN;
    return t - *frame_start;
}

bool vga_in_vretrace(void)
{
    u64 fs;
    u64 ph = frame_phase_ns(host_ns(), &fs);
    u64 period = FRAME_NUM / FRAME_DEN;
    return ph >= period * RETRACE_FIRST_LINE / LINES && ph < period * (RETRACE_FIRST_LINE + RETRACE_LINES) / LINES;
}

u8 vga_in(u16 port)
{
    switch (port) {
    case 0x3C1: return (attr_index & 0x1F) < sizeof attr ? attr[attr_index & 0x1F] : 0;
    case 0x3C5: return seq_index < sizeof seq ? seq[seq_index] : 0;
    case 0x3C7: return dac_state;
    case 0x3C8: return dac_windex;
    case 0x3C9: {
        u8 v = dac[dac_rindex][dac_rsub++];
        if (dac_rsub == 3) { dac_rsub = 0; dac_rindex++; }
        return v;
    }
    case 0x3CF: return gc_index < sizeof gc ? gc[gc_index] : 0;
    case 0x3D5: return crtc_index < sizeof crtc ? crtc[crtc_index] : 0;
    case 0x3DA: {
        attr_flipflop = false;
        u64 fs;
        u64 ph = frame_phase_ns(host_ns(), &fs);
        u64 line_ns = FRAME_NUM / FRAME_DEN / LINES;
        bool vr = vga_in_vretrace();
        bool hblank = (ph % line_ns) >= line_ns * 640 / 800;
        return (u8)((vr ? 0x08 : 0) | ((vr || hblank) ? 0x01 : 0));
    }
    default: return 0xFF;
    }
}

static u64 next_retrace_start(u64 t)
{
    u64 fs;
    u64 ph = frame_phase_ns(t, &fs);
    u64 period = FRAME_NUM / FRAME_DEN;
    u64 rs = period * RETRACE_FIRST_LINE / LINES;
    return ph < rs ? fs + rs : fs + FRAME_NUM / FRAME_DEN + rs;
}

void vga_wait_vretrace_end(void)
{
    while (vga_in_vretrace()) host_pump();
}

void vga_wait_vretrace_start(void)
{
    vga_wait_vretrace_end();
    host_wait_until_ns(next_retrace_start(host_ns()));
}

void vga_wait_vretrace_active(void)
{
    if (vga_in_vretrace()) return;
    host_wait_until_ns(next_retrace_start(host_ns()));
}
