/* Borland C++ 3.0 RTL: __vprinter and the printf family, the number/string conversions (__longtoa,
 * itoa, ltoa, ultoa, atol, atoi) and INTRO's sscanf. Addresses INTRO / PS / AP.
 *
 * Variadic arguments: the C prototypes take the words of the original stack image as unsigned
 * arguments (rtl.h). A dry run of __vprinter counts how many words the format consumes (the count
 * does not depend on the values), then the words are copied onto the emulated DGROUP stack and the
 * real run reads them from there, exactly like the original reads its stack. */
#include "platform/rtl_priv.h"

#include <stdarg.h>

#include "host.h"

/* INTRO 0000:08fc / PS 0000:1c6e / AP 0000:2027 __longtoa(value, buf, radix, signed, letter) */
static u16 longtoa(u32 value, u16 buf, u16 radix, bool sign, u8 letter)
{
    u16 di = buf;
    if (radix <= 36 && (u8)radix >= 2) {
        if ((s32)value < 0 && sign) {
            DSB(di++) = '-';
            value = (u32)-(s32)value;
        }
        u8 tmp[34];
        int n = 0;
        do { tmp[n++] = (u8)(value % radix); value /= radix; } while (value);
        while (n--) {
            u8 d = tmp[n];
            DSB(di++) = d < 10 ? (u8)('0' + d) : (u8)(d - 10 + letter);
        }
    }
    DSB(di) = 0;
    return buf;
}

/* INTRO 0000:27bb / PS 0000:343d / AP 0000:3d02 itoa */
u16 rtl_itoa(s16 value, u16 buf_ds, s16 radix)
{
    u32 v = radix == 10 ? (u32)(s32)value : (u32)(u16)value;
    return longtoa(v, buf_ds, (u16)radix, true, 'a');
}

/* INTRO 0000:27fd / PS 0000:347f / AP 0000:3d44 ltoa */
u16 rtl_ltoa(s32 value, u16 buf_ds, s16 radix)
{
    return longtoa((u32)value, buf_ds, (u16)radix, radix == 10, 'a');
}

/* INTRO 0000:27e3 / PS 0000:3465 / AP 0000:3d2a ultoa */
u16 rtl_ultoa(u32 value, u16 buf_ds, s16 radix)
{
    return longtoa(value, buf_ds, (u16)radix, false, 'a');
}

/* INTRO 0000:1dd0 / PS 0000:2ab4 / AP 0000:3340 atol */
s32 rtl_atol(u16 s_ds)
{
    u16 si = s_ds;
    u16 ct = RL(ctype1);
    u8 bl;
    do bl = DSB(si++); while (DSB((u16)(ct + bl)) & 1);    /* skip _IS_SP */
    bool neg = false;
    if (bl == '+' || bl == '-') { neg = bl == '-'; bl = DSB(si++); }
    u32 v = 0;
    while (bl >= '0' && bl <= '9') {
        v = v * 10 + (u32)(bl - '0');
        bl = DSB(si++);
    }
    return neg ? (s32)(0u - v) : (s32)v;
}

/* INTRO 0000:1e40 / PS 0000:2b24 / AP 0000:33b0 atoi */
s16 rtl_atoi(u16 s_ds) { return (s16)rtl_atol(s_ds); }

/* ------------------------------------------------------------------ __vprinter */

typedef struct {
    RtlPutFn put;
    u16 put_arg;
    u16 va;                    /* DS offset of the next argument word (dry run: word counter) */
    bool dry;
    u16 buf;                   /* the 80-byte output buffer (DS) */
    u16 di;                    /* output position in buf */
    u8 count;                  /* free bytes left in buf */
    u16 total;
    bool error;
} Pr;

static u16 va_word(Pr *p)
{
    u16 w = p->dry ? 0 : DSW(p->va);
    p->va = (u16)(p->va + 2);
    return w;
}

/* INTRO 0000:1367 / PS 0000:2057 / AP 0000:23c3: flush the buffer through the output function */
static void pr_flush(Pr *p)
{
    u16 len = (u16)(p->di - p->buf);
    if (!p->dry && p->put(len, p->buf, p->put_arg) == 0) p->error = true;
    p->count = 0x50;
    p->total = (u16)(p->total + len);
    p->di = p->buf;
}

/* INTRO 0000:135e / ...: store one character */
static void pr_out(Pr *p, u8 c)
{
    if (!p->dry) DSB(p->di) = c;
    p->di++;
    if (--p->count == 0) pr_flush(p);
}

static u16 pr_strlen(FarPtr s) { u16 n = 0; while (rd8(s.seg, (u16)(s.off + n))) n++; return n; }

/* 4 upper-case hex digits (INTRO 0000:131f.. / AP 0000:237b) */
static void hex4(u8 *out, u16 v)
{
    for (int i = 0; i < 4; i++) {
        u8 d = (u8)((v >> (12 - 4 * i)) & 0xF);
        out[i] = d < 10 ? (u8)('0' + d) : (u8)('A' + d - 10);
    }
}

/* INTRO 0000:1337 / PS 0000:2027 / AP 0000:2393 __vprinter(put, arg, fmt, va) */
static s16 vprinter(Pr *p, u16 fmt)
{
    u16 m = stk_mark();
    p->buf = p->dry ? 0 : stk_alloc(0x50);
    p->di = p->buf;
    p->count = 0x50;
    p->total = 0;
    p->error = false;
    u16 si = fmt;
    const u16 cls = RL(pf_class);
    for (;;) {
        u8 al = DSB(si++);
        if (!al) break;
        if (al != '%') { pr_out(p, al); continue; }
        u16 pct = si;                                /* [bp-10h]: just after '%' */
        al = DSB(si++);
        if (al == '%') { pr_out(p, '%'); continue; }
        u16 out_save = p->di;                        /* [bp-4] */
        u8 state = 0;                                /* CH */
        s16 zeros = 0;                               /* [bp-0Eh] */
        u16 flags = 0;                               /* [bp-2]: 1 #, 2 -, 4 nonzero, 8 0-pad, 10h l,
                                                        20h far, 40h 0x prefix, 100h L */
        u8 sign = 0;                                 /* [bp-0Bh] */
        s16 width = -1, prec = -1;                   /* [bp-8], [bp-0Ah] */
        u8 conv = 0;                                 /* [bp-5] */
        u8 nbuf[0x48];                               /* [bp-46h].. number / text buffer (host) */
        FarPtr str = { 0, 0 };
        u16 len = 0;
        bool bad = false, done_conv = false;
        for (;;) {
            u8 c = al;
            u8 k = (u8)(c - 0x20);
            if (k >= 0x60) { bad = true; break; }
            u8 h = DSB((u16)(cls + k));
            if (h > 0x17) { bad = true; break; }
            switch (h) {
            case 0:  /* ' ' '+' */
                if (state) { bad = true; break; }
                if (sign != '+') sign = c;
                break;
            case 1:  /* '#' */
                if (state) { bad = true; break; }
                flags |= 1;
                break;
            case 3:  /* '-' */
                if (state) { bad = true; break; }
                flags |= 2;
                break;
            case 22: flags &= ~0x20; state = 5; break;          /* 'N' */
            case 23: flags |= 0x20; state = 5; break;           /* 'F' */
            case 9:  /* '0' */
                if (state == 0) {
                    if (!(flags & 2)) { flags |= 8; state = 1; }
                    break;
                }
                __attribute__((fallthrough));   /* a digit */
            case 5: {
                s16 d = (s16)(c - '0');
                if (state <= 2) {
                    state = 2;
                    s16 old = width;
                    width = d;
                    if (old >= 0) width = (s16)(d + old * 10);
                } else if (state == 4) {
                    s16 old = prec;
                    prec = d;
                    if (old != 0) prec = (s16)(d + old * 10);
                } else bad = true;
                break;
            }
            case 2: { /* '*' */
                s16 v = (s16)va_word(p);
                if (state < 2) {
                    if (v < 0) { v = (s16)-v; flags |= 2; }
                    width = v;
                    state = 3;
                } else if (state == 4) {
                    prec = v;
                    state = 5;
                } else bad = true;
                break;
            }
            case 4:  /* '.' */
                if (state >= 4) { bad = true; break; }
                state = 4;
                prec++;
                break;
            case 6: flags |= 0x10; state = 5; break;            /* 'l' */
            case 7: flags |= 0x100; flags &= ~0x10; state = 5; break;   /* 'L' */
            case 8: flags &= ~0x10; state = 5; break;           /* 'h' */
            case 11: case 12: case 13: case 10: {                /* o u x X d i */
                u16 radix = h == 11 ? 8 : h == 13 ? 16 : 10;
                u8 letter = h == 13 ? (u8)(0xE9 + c) : 'a';
                bool is_signed = h == 10;
                if (h == 11 || h == 13) sign = 0;
                conv = c;
                u32 v = va_word(p);
                if (is_signed) v = (u32)(s32)(s16)v;
                if (flags & 0x10) v = (v & 0xFFFF) | (u32)va_word(p) << 16;
                str = far_make(0, 1);                         /* marks "in nbuf" */
                if (v == 0 && prec == 0) {
                    nbuf[1] = 0;
                } else {
                    if (v != 0) flags |= 4;
                    /* __longtoa into nbuf+1 (a spare byte in front for the sign) */
                    u8 tmp[34];
                    int n = 0, o = 1;
                    u32 x = v;
                    if ((s32)x < 0 && is_signed) { nbuf[o++] = '-'; x = (u32)-(s32)x; }
                    do { tmp[n++] = (u8)(x % radix); x /= radix; } while (x);
                    while (n--) nbuf[o++] = tmp[n] < 10 ? (u8)('0' + tmp[n]) : (u8)(tmp[n] - 10 + letter);
                    nbuf[o] = 0;
                }
                /* 25ac: zero padding */
                int start = 1;
                s16 dx;
                bool pad = false;
                if (prec >= 0) { dx = prec; pad = true; }
                else if ((flags & 8) && width > 0) { dx = width; pad = true; }
                if (pad) {
                    s16 cx = 0;
                    while (nbuf[start + cx]) cx++;
                    if (nbuf[start] == '-') cx--;
                    dx = (s16)(dx - cx);
                    if (dx > 0) zeros = dx;
                }
                if (nbuf[start] != '-') {
                    if (sign) { nbuf[--start] = sign; goto adj; }
                } else {
adj:
                    if (zeros > 0 && prec < 0) zeros--;
                }
                len = 0;
                while (nbuf[start + len]) len++;
                str = far_make(0xFFFF, (u16)start);
                done_conv = true;
                break;
            }
            case 14: {                                            /* 'p' */
                conv = c;
                u16 off = va_word(p);
                int o = 0;
                if (flags & 0x20) {
                    u16 seg = va_word(p);
                    hex4(nbuf + o, seg);
                    o += 4;
                    nbuf[o++] = ':';
                }
                hex4(nbuf + o, off);
                o += 4;
                nbuf[o] = 0;
                flags &= ~4;
                if ((flags & 8) && width > 0) {             /* 26aa with the zero flag */
                    s16 cx = (s16)o;
                    if (nbuf[0] == '-') cx--;
                    s16 dx = (s16)(width - cx);
                    if (dx > 0) zeros = dx;
                }
                int start = 0;
                if (nbuf[0] != '-') {
                    if (sign) {
                        for (int i = o; i >= 0; i--) nbuf[i + 1] = nbuf[i];
                        nbuf[0] = sign;
                        if (zeros > 0 && prec < 0) zeros--;
                    }
                }
                len = 0;
                while (nbuf[start + len]) len++;
                str = far_make(0xFFFF, (u16)start);
                done_conv = true;
                break;
            }
            case 16: {                                            /* 'c' */
                conv = c;
                u16 w = va_word(p);
                nbuf[0] = (u8)w;
                nbuf[1] = 0;
                len = 1;
                str = far_make(0xFFFF, 0);
                done_conv = true;
                break;
            }
            case 17: {                                            /* 's' */
                conv = c;
                FarPtr s;
                if (!(flags & 0x20)) s = far_make(cur_dgroup, va_word(p));
                else { u16 o = va_word(p); u16 sg = va_word(p); s = far_make(sg, o); }
                bool null = (flags & 0x20) ? far_is_null(s) : s.off == 0;
                if (null) s = far_make(cur_dgroup, RL(null_str));
                len = p->dry ? 0 : pr_strlen(s);
                if (len > (u16)prec) len = (u16)prec;
                str = s;
                done_conv = true;
                break;
            }
            case 15:                                              /* e f g E G */
                host_fatal("printf : floating point formats not linked");
            case 18: {                                            /* 'n' */
                FarPtr d;
                if (!(flags & 0x20)) d = far_make(cur_dgroup, va_word(p));
                else { u16 o = va_word(p); u16 sg = va_word(p); d = far_make(sg, o); }
                if (!p->dry) {
                    u16 nout = (u16)(0x50 - p->count + p->total);
                    wr16(d.seg, d.off, nout);
                    if (flags & 0x10) wr16(d.seg, (u16)(d.off + 2), 0);
                }
                p->di = out_save;
                done_conv = true;
                str.seg = 0x0001;                            /* nothing to emit */
                break;
            }
            default:
                bad = true;
                break;
            }
            if (bad || done_conv) break;
            al = DSB(si++);
        }
        if (bad) {
            /* 27e6: print '%' and the rest of the format verbatim, then stop */
            si = pct;
            p->di = out_save;
            pr_out(p, '%');
            for (;;) {
                u8 c = DSB(si++);
                if (!c) break;
                pr_out(p, c);
            }
            break;
        }
        if (str.seg == 0x0001) continue;             /* %n */
        /* 26ed: emit with width, zeros, prefixes */
        p->di = out_save;
        s16 bx = width;
        u16 cx = len;
        if ((flags & 5) == 5) {
            if (conv == 'o') { if (zeros <= 0) zeros = 1; }
            else if (conv == 'x' || conv == 'X') {
                flags |= 0x40;
                bx = (s16)(bx - 2);
                zeros = (s16)(zeros - 2);
                if (zeros < 0) zeros = 0;
            }
        }
        cx = (u16)(cx + zeros);
        if (!(flags & 2))
            while (bx > (s16)cx) { pr_out(p, ' '); bx--; }
        if (flags & 0x40) { pr_out(p, '0'); pr_out(p, conv); }
        u16 pos = 0;
        #define CH(i) (str.seg == 0xFFFF ? nbuf[str.off + (i)] : rd8(str.seg, (u16)(str.off + (i))))
        if (zeros > 0) {
            cx = (u16)(cx - zeros);
            bx = (s16)(bx - zeros);
            u8 c0 = len ? CH(0) : 0;
            if (c0 == '-' || c0 == ' ' || c0 == '+') {
                pr_out(p, c0);
                pos++;
                cx--;
                bx--;
            }
            for (s16 z = zeros; z; z--) pr_out(p, '0');
        }
        if (cx) {
            bx = (s16)(bx - cx);
            for (u16 i = 0; i < cx; i++) pr_out(p, p->dry ? 0 : CH(pos + i));
        }
        #undef CH
        if (bx > 0) for (s16 i = bx; i; i--) pr_out(p, ' ');
    }
    if (p->count < 0x50) pr_flush(p);
    stk_release(m);
    return p->error ? -1 : (s16)p->total;
}

s16 rtl___vprinter(RtlPutFn put, u16 put_arg, u16 fmt_ds, u16 va_ds)
{
    Pr p = { put, put_arg, va_ds, false, 0, 0, 0, 0, false };
    return vprinter(&p, fmt_ds);
}

int rtl_va_count(u16 fmt_ds)
{
    Pr p = { NULL, 0, 0, true, 0, 0, 0, 0, false };
    vprinter(&p, fmt_ds);
    return p.va / 2;
}

u16 rtl_va_words(u16 fmt_ds, const u16 *words, int nwords)
{
    (void)fmt_ds;
    u16 va = stk_alloc((u16)(2 * nwords + 2));
    for (int i = 0; i < nwords; i++) DSW((u16)(va + 2 * i)) = words[i];
    return va;
}

u16 rtl_va_from_list(u16 fmt_ds, va_list ap)
{
    int n = rtl_va_count(fmt_ds);
    u16 w[64];
    if (n > 64) host_fatal("printf: too many arguments");
    for (int i = 0; i < n; i++) w[i] = (u16)va_arg(ap, unsigned);
    return rtl_va_words(fmt_ds, w, n);
}

/* Collects the variadic words of a format call onto the emulated stack. */
#define COLLECT(fmt, last, va_out)                                            \
    u16 mark_ = stk_mark();                                                   \
    u16 va_out;                                                               \
    {                                                                         \
        int n_ = rtl_va_count(fmt);                                           \
        u16 w_[64];                                                           \
        if (n_ > 64) host_fatal("printf: too many arguments");                \
        va_list ap_;                                                          \
        va_start(ap_, last);                                                  \
        for (int i_ = 0; i_ < n_; i_++) w_[i_] = (u16)va_arg(ap_, unsigned);  \
        va_end(ap_);                                                          \
        va_out = rtl_va_words(fmt, w_, n_);                                   \
    }

static u16 put_file(u16 n, u16 s, u16 fp) { return rtl___fputn(fp, n, s); }

/* PS 0000:3e34 / AP 0000:4719: sprintf's output (pp = DS offset of the buffer pointer) */
static u16 put_str(u16 n, u16 s, u16 pp)
{
    u16 dst = DSW(pp);
    rtl_memcpy(dst, s, n);
    DSW(pp) = (u16)(dst + n);
    DSB(DSW(pp)) = 0;
    return n;
}

/* INTRO 0000:22c3 / PS 0000:2f45 / AP 0000:37d1 fprintf */
s16 rtl_fprintf(u16 fp_ds, u16 fmt_ds, ...)
{
    COLLECT(fmt_ds, fmt_ds, va);
    s16 r = rtl___vprinter(put_file, fp_ds, fmt_ds, va);
    stk_release(mark_);
    return r;
}

/* INTRO 0000:2a52 / PS 0000:3778 / AP 0000:401e printf */
s16 rtl_printf(u16 fmt_ds, ...)
{
    COLLECT(fmt_ds, fmt_ds, va);
    s16 r = rtl___vprinter(put_file, rtl_stdout_fp(), fmt_ds, va);
    stk_release(mark_);
    return r;
}

/* INTRO 0000:36e1 / PS 0000:45a3 vprintf */
s16 rtl_vprintf(u16 fmt_ds, u16 va_ds)
{
    return rtl___vprinter(put_file, rtl_stdout_fp(), fmt_ds, va_ds);
}

/* PS 0000:3e5c / AP 0000:4741 sprintf */
s16 rtl_sprintf(u16 buf_ds, u16 fmt_ds, ...)
{
    COLLECT(fmt_ds, fmt_ds, va);
    DSB(buf_ds) = 0;
    u16 pp = stk_alloc(2);                           /* the original passes &buf (its own arg slot) */
    DSW(pp) = buf_ds;
    s16 r = rtl___vprinter(put_str, pp, fmt_ds, va);
    stk_release(mark_);
    return r;
}

/* PS 0000:3e79 / AP 0000:475e vsprintf */
s16 rtl_vsprintf(u16 buf_ds, u16 fmt_ds, u16 va_ds)
{
    u16 m = stk_mark();
    DSB(buf_ds) = 0;
    u16 pp = stk_alloc(2);
    DSW(pp) = buf_ds;
    s16 r = rtl___vprinter(put_str, pp, fmt_ds, va_ds);
    stk_release(m);
    return r;
}

/* ------------------------------------------------------------------ INTRO sscanf */

/* INTRO 0000:2f68 sscanf / 0000:2f86 vsscanf / 0000:0b0a _scanner. Only the dead RESOURCE.CFG
 * reader (config_read 038e:0284) calls it. PORT: a compact _scanner covering the conversions of
 * Borland's (d i u o x c s n, h/l/N/F modifiers, '*' suppression, widths); no floats, no [sets]. */
static s16 vsscanf_core(u16 s, u16 fmt, u16 va)
{
    u16 si = s, fi = fmt;
    s16 assigned = 0;
    u16 ct = RL(ctype1);
    bool any_input = false;
    for (;;) {
        u8 f = DSB(fi++);
        if (!f) break;
        if (DSB((u16)(ct + f)) & 1) {                 /* white space matches any amount */
            while (DSB((u16)(ct + DSB(si))) & 1) si++;
            continue;
        }
        if (f != '%') {
            if (DSB(si) != f) break;
            si++;
            continue;
        }
        f = DSB(fi++);
        if (f == '%') { if (DSB(si) != '%') break; si++; continue; }
        bool suppress = false, lng = false, far_p = false;
        if (f == '*') { suppress = true; f = DSB(fi++); }
        int width = 0;
        while (f >= '0' && f <= '9') { width = width * 10 + (f - '0'); f = DSB(fi++); }
        for (;;) {
            if (f == 'h') lng = false;
            else if (f == 'l') lng = true;
            else if (f == 'F') far_p = true;
            else if (f == 'N') far_p = false;
            else break;
            f = DSB(fi++);
        }
        FarPtr dst = { 0, 0 };
        if (!suppress && f != 0) {
            u16 o = DSW(va); va = (u16)(va + 2);
            dst = far_make(cur_dgroup, o);
            if (far_p) { dst.seg = DSW(va); va = (u16)(va + 2); }
        }
        if (f == 'n') { if (!suppress) wr16(dst.seg, dst.off, (u16)(si - s)); continue; }
        if (f != 'c') while (DSB((u16)(ct + DSB(si))) & 1) si++;
        if (!DSB(si)) return any_input ? assigned : -1;
        if (width == 0) width = f == 'c' ? 1 : 0x7FFF;
        if (f == 'c' || f == 's') {
            u16 o = dst.off;
            int n = 0;
            while (n < width && DSB(si) && (f == 'c' || !(DSB((u16)(ct + DSB(si))) & 1))) {
                if (!suppress) wr8(dst.seg, o++, DSB(si));
                si++;
                n++;
            }
            if (f == 's' && !suppress) wr8(dst.seg, o, 0);
            any_input = true;
            if (!suppress) assigned++;
            continue;
        }
        int base = f == 'o' ? 8 : f == 'x' || f == 'X' ? 16 : 10;
        if (f != 'd' && f != 'i' && f != 'u' && f != 'o' && f != 'x' && f != 'X' && f != 'D' && f != 'U')
            break;
        if (f == 'D' || f == 'U') lng = true;
        bool neg = false;
        int n = 0;
        if (DSB(si) == '-' || DSB(si) == '+') { neg = DSB(si) == '-'; si++; n++; }
        u32 v = 0;
        int digits = 0;
        for (; n < width; n++) {
            u8 c = DSB(si);
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
            if (d >= base) break;
            v = v * (u32)base + (u32)d;
            si++;
            digits++;
        }
        if (!digits) break;
        any_input = true;
        if (neg) v = 0u - v;
        if (!suppress) {
            wr16(dst.seg, dst.off, (u16)v);
            if (lng) wr16(dst.seg, (u16)(dst.off + 2), (u16)(v >> 16));
            assigned++;
        }
    }
    return assigned;
}

static int scanf_words(u16 fmt)
{
    int n = 0;
    for (u16 fi = fmt;;) {
        u8 f = DSB(fi++);
        if (!f) break;
        if (f != '%') continue;
        f = DSB(fi++);
        if (f == '%') continue;
        bool suppress = f == '*', far_p = false;
        if (suppress) f = DSB(fi++);
        while ((f >= '0' && f <= '9') || f == 'h' || f == 'l' || f == 'F' || f == 'N') {
            if (f == 'F') far_p = true;
            if (f == 'N') far_p = false;
            f = DSB(fi++);
        }
        if (!f) break;
        if (!suppress) n += far_p ? 2 : 1;
    }
    return n;
}

/* INTRO 0000:2f68 sscanf */
s16 rtl_sscanf(u16 s_ds, u16 fmt_ds, ...)
{
    u16 m = stk_mark();
    int n = scanf_words(fmt_ds);
    u16 w[32];
    if (n > 32) host_fatal("sscanf: too many arguments");
    va_list ap;
    va_start(ap, fmt_ds);
    for (int i = 0; i < n; i++) w[i] = (u16)va_arg(ap, unsigned);
    va_end(ap);
    u16 va = rtl_va_words(fmt_ds, w, n);
    s16 r = vsscanf_core(s_ds, fmt_ds, va);
    stk_release(m);
    return r;
}
