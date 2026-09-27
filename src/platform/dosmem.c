/* DOS memory and huge pointers (platform.md §5.1-5.2): INTRO 05da:35e0.., 4611..4843, PS 16e3:3bf4..,
 * 4935..4b67, AP 1d32:5a2e.., 676f..69a1. Far memory = DOS blocks of the arena in mem[] (mem.c:
 * INT 21h 48h/49h/4Ah model), paragraph aligned, offset 0, never moved. Huge-pointer arithmetic on
 * 32-bit linear addresses, keeping the normalised form where the original stores it. */
#include "platform/rtl_priv.h"

#include <string.h>

#include "host.h"

/* INTRO 05da:35e0 / PS 16e3:3bf4 / AP 1d32:5a2e dos_alloc(size, unused, flags) */
FarPtr dos_alloc(u32 size, u16 a1, u16 flags)
{
    (void)a1;
    if (size == 0xFFFFFFFFu) {                        /* query: the largest free block in bytes */
        u32 b = (u32)dos_mem_largest() << 4;
        return far_make((u16)(b >> 16), (u16)b);     /* DX:AX (not a pointer) */
    }
    u16 par = (u16)(size >> 4);                       /* 16-bit: sizes >= 1 MB wrap */
    if (size & 0xF) par++;
    u16 seg;
    if (!dos_mem_alloc(par, &seg)) return far_make(0, 0);
    if (flags & 1) huge_memset_fp(far_make(seg, 0), 0, size);
    return far_make(seg, 0);
}

/* INTRO 05da:3657 / PS 16e3:3c6b / AP 1d32:5aa5 dos_free: INT 21h/49h ES = seg (errors ignored) */
void dos_free(FarPtr p)
{
    dos_mem_free(p.seg);
}

/* INTRO 05da:4611 / PS 16e3:4935 / AP 1d32:676f huge_norm: DX += AX >> 4, AX &= 0Fh */
FarPtr huge_norm(FarPtr p)
{
    return far_make((u16)(p.seg + (p.off >> 4)), (u16)(p.off & 0xF));
}

/* INTRO 05da:4623 / PS 16e3:4947 / AP 1d32:6781 huge_norm_top: the largest-offset form (backward
 * copies) */
FarPtr huge_norm_top(FarPtr p)
{
    p = huge_norm(p);
    if (p.seg & 0xF000) return far_make((u16)(p.seg - 0x0FFF), (u16)(p.off + 0xFFF0));
    return far_make(0, (u16)(p.off + (p.seg << 4)));
}

/* INTRO 05da:4640 / PS 16e3:4964 / AP 1d32:679e huge_add: DX:AX += CX:BX bytes (not normalised) */
FarPtr huge_add(FarPtr p, u32 n)
{
    u32 o = (u32)p.off + (u16)n;
    u16 seg = p.seg;
    if (o > 0xFFFF) seg = (u16)(seg + 0x1000);
    u16 cx = (u16)(n >> 16);
    u32 r = ((u32)cx >> 5) | ((u32)cx << 12);        /* RCR CX,5 with CF = 0 (17-bit rotate) */
    seg = (u16)(seg + (u16)r);
    return far_make(seg, (u16)o);
}

/* INTRO 05da:4654 / PS 16e3:4978 / AP 1d32:67b2 huge_sub: DX:AX -= CX:BX, normalised */
FarPtr huge_sub(FarPtr p, u32 n)
{
    u16 bx = (u16)-(u16)n;
    u32 o = (u32)p.off + bx;
    u16 seg = p.seg;
    u16 borrow = (o > 0xFFFF) ? 0 : 0x1000;          /* SBB BX,BX; NOT BX; AND 1000h */
    seg = (u16)(seg - borrow);
    FarPtr r = huge_norm(far_make(seg, (u16)o));
    u16 cx = (u16)(n >> 16);
    u32 rc = ((u32)cx >> 5) | ((u32)cx << 12);
    r.seg = (u16)(r.seg - (u16)rc);
    return r;
}

/* INTRO 05da:4836 / PS 16e3:4b5a / AP 1d32:6994 huge_norm_far */
FarPtr huge_norm_far_fp(FarPtr p) { return huge_norm(p); }

/* INTRO 05da:480a / PS 16e3:4b2e / AP 1d32:6968 huge_cmp: -1 / 0 / 1 on the normalised pointers */
s16 huge_cmp(FarPtr a, FarPtr b)
{
    a = huge_norm(a);
    b = huge_norm(b);
    if (a.seg != b.seg) return a.seg < b.seg ? -1 : 1;
    if (a.off > b.off) return 1;
    return a.off < b.off ? -1 : 0;
}

static u8 *lin_ptr(u32 a, u32 n)
{
    if (a + n > MEM_SIZE) host_fatal("huge access beyond memory (%05X + %X)", (unsigned)a, (unsigned)n);
    if (a < 0xB0000 && a + n > 0xA0000) host_fatal("huge access to video memory at %05X", (unsigned)a);
    return mem + a;
}

/* INTRO 05da:469d / PS 16e3:49c1 / AP 1d32:67fb far_memmove_fp(dst, src, n): overlap-safe, in pieces of
 * at most 7D00h bytes; returns the original dst */
FarPtr far_memmove_fp(FarPtr dst, FarPtr src, u32 n)
{
    if (n == 0) return dst;
    u32 d = far_lin(dst), s = far_lin(src);
    if (d != s) memmove(lin_ptr(d, n), lin_ptr(s, n), n);
    return dst;
}

/* INTRO 05da:4776 / PS 16e3:4a9a / AP 1d32:68d4 huge_memcpy_fp(dst, src, n): normalised, one forward
 * copy (odd byte to align DI, words, last byte) */
void huge_memcpy_fp(FarPtr dst, FarPtr src, u16 n)
{
    if (!n) return;
    u32 d = far_lin(dst), s = far_lin(src);
    u8 *dp = lin_ptr(d, n), *sp = lin_ptr(s, n);
    u16 cx = n;
    if (d & 1) { *dp++ = *sp++; cx--; }
    for (u16 i = cx >> 1; i; i--) { u8 a = sp[0], b = sp[1]; dp[0] = a; dp[1] = b; dp += 2; sp += 2; }
    if (cx & 1) *dp = *sp;
}

/* INTRO 05da:47b0 / PS 16e3:4ad4 / AP 1d32:690e huge_memset_fp(dst, v, n) */
void huge_memset_fp(FarPtr dst, u8 v, u32 n)
{
    if (!n) return;
    memset(lin_ptr(far_lin(dst), n), v, n);
}

/* Word-list entry points (the original frames) for callers outside the platform module. */
void far_memmove(u16 a0, u16 a1, u16 a2, u16 a3, u16 a4, u16 a5)
{
    far_memmove_fp(far_make(a1, a0), far_make(a3, a2), (u32)a5 << 16 | a4);
}
void huge_memset(u16 a0, u16 a1, u16 a2, u16 a3, u16 a4) { huge_memset_fp(far_make(a1, a0), (u8)a2, (u32)a4 << 16 | a3); }
void huge_memcpy(u16 a0, u16 a1, u16 a2, u16 a3, u16 a4) { huge_memcpy_fp(far_make(a1, a0), far_make(a3, a2), a4); }
u32 huge_norm_far(u16 a0, u16 a1) { FarPtr p = huge_norm(far_make(a1, a0)); return (u32)p.seg << 16 | p.off; }
