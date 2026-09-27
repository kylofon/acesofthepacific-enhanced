/* Borland C++ 3.0 RTL stdio (medium model): FILE records in _streams[] (DGROUP), buffers from the
 * near heap, exactly as the original (fopen allocates its 512-byte buffer through setvbuf; stdin gets
 * one at start-up). Also the far string helpers (_fstr*, _fmem*). Addresses INTRO / PS / AP. */
#include "platform/rtl_priv.h"

#include "host.h"

#define FL(fp)   DSW((u16)((fp) + FILE_level))
#define FLS(fp)  DSS((u16)((fp) + FILE_level))
#define FF(fp)   DSW((u16)((fp) + FILE_flags))
#define FFD(fp)  DSC((u16)((fp) + FILE_fd))
#define FBS(fp)  DSW((u16)((fp) + FILE_bsize))
#define FBUF(fp) DSW((u16)((fp) + FILE_buffer))
#define FCUR(fp) DSW((u16)((fp) + FILE_curp))
#define FTMP(fp) DSW((u16)((fp) + FILE_istemp))
#define FTOK(fp) DSW((u16)((fp) + FILE_token))

static u16 stream(int i) { return (u16)(RL(streams) + FILE_SIZE * i); }
u16 rtl_stdout_fp(void) { return stream(1); }
static u16 openfd_of(u16 fp) { return DSW((u16)(RL(openfd) + 2 * FFD(fp))); }

static void rtl_set_hook(u16 field, u16 off)
{
    ds_far_wr(field, far_make(SEG(0), off));        /* void (far *)(): relocated 0000:off */
}

/* INTRO 0000:2e65 / PS 0000:3d5c / AP 0000:4641 setvbuf */
s16 rtl_setvbuf(u16 fp, u16 buf_ds, s16 type, u16 size)
{
    if (FTOK(fp) != fp || type > 2 || size > 0x7FFF) return -1;
    if (!DSW(RL(stdout_setbuf)) && fp == stream(1)) DSW(RL(stdout_setbuf)) = 1;
    else if (!DSW(RL(stdin_setbuf)) && fp == stream(0)) DSW(RL(stdin_setbuf)) = 1;
    if (FL(fp) != 0) rtl_fseek(fp, 0, 1);
    if (FF(fp) & F_BUF) rtl_free(FBUF(fp));
    FF(fp) &= 0xFFF3;
    FBS(fp) = 0;
    FBUF(fp) = FCUR(fp) = (u16)(fp + FILE_hold);
    if (type != 2 && size > 0) {
        rtl_set_hook(RL(exitbuf), RTL_XFFLUSH_OFF);
        if (buf_ds == 0) {
            buf_ds = rtl_malloc(size);
            if (!buf_ds) return -1;
            FF(fp) |= F_BUF;
        }
        FCUR(fp) = FBUF(fp) = buf_ds;
        FBS(fp) = size;
        if (type == 1) FF(fp) |= F_LBUF;
    }
    return 0;
}

/* INTRO 0000:114f / PS 0000:1e13 / AP 0000:2256 setbuf */
void rtl_setbuf(u16 fp_ds, u16 buf_ds)
{
    rtl_setvbuf(fp_ds, buf_ds, buf_ds ? 0 : 2, 0x200);
}

/* INTRO 0000:1fa6 / PS 0000:2c8a / AP 0000:3516 fflush (NULL: every stream open for I/O) */
s16 rtl_fflush(u16 fp)
{
    if (fp == 0) {                                   /* INTRO 0000:20e9 flushall */
        u16 n = DSW(RL(nfile));
        for (int i = 0; n; i++, n--)
            if (FF(stream(i)) & F_RDWR) rtl_fflush(stream(i));
        return 0;
    }
    if (FTOK(fp) != fp) return -1;
    if (FLS(fp) >= 0) {
        if (!(FF(fp) & F_LBUF) && FCUR(fp) != (u16)(fp + FILE_hold)) return 0;
        FL(fp) = 0;
        if (FCUR(fp) == (u16)(fp + FILE_hold)) FCUR(fp) = FBUF(fp);
        return 0;
    }
    u16 cnt = (u16)(FBS(fp) + FL(fp) + 1);
    FL(fp) = (u16)(FL(fp) - cnt);
    FCUR(fp) = FBUF(fp);
    s16 w = rtl_write(FFD(fp), FBUF(fp), cnt);
    if ((u16)w != cnt && !(FF(fp) & F_TERM)) {
        FF(fp) |= F_ERR;
        return -1;
    }
    return 0;
}

/* INTRO 0000:260c / PS 0000:328e / AP 0000:3b1a: flush the line-buffered terminal streams */
static void flush_term_lines(void)
{
    for (int i = 0; i < 0x14; i++)
        if ((FF(stream(i)) & 0x300) == 0x300) rtl_fflush(stream(i));
}

/* INTRO 0000:1f25 / PS 0000:2c09 / AP 0000:3495 fclose */
s16 rtl_fclose(u16 fp)
{
    s16 r = -1;
    if (fp == 0 || FTOK(fp) != fp) return -1;
    if (FBS(fp)) {
        if (FLS(fp) < 0 && rtl_fflush(fp)) return -1;
        if (FF(fp) & F_BUF) rtl_free(FBUF(fp));
    }
    if (FFD(fp) >= 0) r = rtl_close(FFD(fp));
    FF(fp) = 0;
    FBS(fp) = 0;
    FL(fp) = 0;
    DSB((u16)(fp + FILE_fd)) = 0xFF;
    if (FTMP(fp)) host_fatal("fclose of a tmpfile() stream is not modelled");
    return r;
}

/* INTRO 0000:2123 / PS 0000:2da5 / AP 0000:3631 __mode: fopen mode string -> FILE flags, open flags,
 * permission. Also installs _exitfopen. */
static u16 fopen_mode(u16 mode_ds, u16 *oflag, u16 *pmode)
{
    u16 si = mode_ds;
    u16 pm = 0, dx, di;
    u8 cl = DSB(si++);
    if (cl == 'r') { dx = 1; di = 1; }
    else if (cl == 'w') { dx = 0x302; pm = 0x80; di = 2; }
    else if (cl == 'a') { dx = 0x902; pm = 0x80; di = 2; }
    else return 0;
    cl = DSB(si++);
    if (cl == '+' || (DSB(si) == '+' && (cl == 't' || cl == 'b'))) {
        if (cl == '+') cl = DSB(si);
        dx = (u16)((dx & 0xFFFC) | 4);
        pm = 0x180;
        di = 3;
    }
    if (cl == 't') dx |= 0x4000;
    else if (cl == 'b') { dx |= 0x8000; di |= 0x40; }
    else {
        dx |= DSW(RL(fmode)) & 0xC000;
        if (dx & 0x8000) di |= 0x40;
    }
    rtl_set_hook(RL(exitfopen), RTL_XFCLOSE_OFF);
    *oflag = dx;
    *pmode = pm;
    return di;
}

/* INTRO 0000:21dd / PS 0000:2e5f / AP 0000:36eb __openfp(fp, path, mode, shflag) */
static u16 rtl___openfp(u16 fp, u16 path_ds, u16 mode_ds, u16 shflag)
{
    u16 oflag, pmode;
    FF(fp) = fopen_mode(mode_ds, &oflag, &pmode);
    if (FF(fp) == 0) goto fail;
    if (FFD(fp) < 0) {
        s16 fd = rtl_open(path_ds, (u16)(oflag | shflag), (unsigned)pmode);
        DSB((u16)(fp + FILE_fd)) = (u8)fd;
        if ((s8)fd < 0) goto fail;
    }
    if (rtl_isatty(FFD(fp))) FF(fp) |= F_TERM;
    if (rtl_setvbuf(fp, 0, (FF(fp) & F_TERM) ? 1 : 0, 0x200)) {
        rtl_fclose(fp);
        return 0;
    }
    FTMP(fp) = 0;
    return fp;
fail:
    DSB((u16)(fp + FILE_fd)) = 0xFF;
    FF(fp) = 0;
    return 0;
}

/* INTRO 0000:2279 / PS 0000:2efb / AP 0000:3787 __getfp: first free FILE (the loop reads one entry
 * past _streams[_nfile - 1] when all are in use, as the original) */
static u16 rtl___getfp(void)
{
    u16 si = stream(0);
    u16 end = (u16)(RL(streams) + FILE_SIZE * DSW(RL(nfile)));
    for (;;) {
        if (DSC((u16)(si + FILE_fd)) < 0) break;
        u16 dx = si;
        si = (u16)(si + FILE_SIZE);
        if (!(end > dx)) break;
    }
    return DSC((u16)(si + FILE_fd)) < 0 ? si : 0;
}

/* INTRO 0000:22a4 / PS 0000:2f26 / AP 0000:37b2 fopen */
u16 rtl_fopen(u16 path_ds, u16 mode_ds)
{
    u16 fp = rtl___getfp();
    if (!fp) return 0;
    return rtl___openfp(fp, path_ds, mode_ds, 0);
}

/* INTRO 0000:2a82 / PS 0000:37a8 / AP 0000:404e fputc */
s16 rtl_fputc(s16 c, u16 fp)
{
    u16 chp = RL(fputc_ch);
    DSB(chp) = (u8)c;
    u8 ch = (u8)c;
    if (FLS(fp) < -1) {
        FL(fp) = (u16)(FL(fp) + 1);
        DSB(FCUR(fp)) = ch;
        FCUR(fp) = (u16)(FCUR(fp) + 1);
        if ((FF(fp) & F_LBUF) && (ch == '\n' || ch == '\r'))
            if (rtl_fflush(fp)) return -1;
        return DSB(chp);
    }
    if ((FF(fp) & 0x90) || !(FF(fp) & F_WRIT)) goto err;
    FF(fp) |= F_OUT;
    if (FBS(fp)) {
        if (FL(fp) != 0 && rtl_fflush(fp)) return -1;
        FL(fp) = (u16)-FBS(fp);
        DSB(FCUR(fp)) = DSB(chp);
        FCUR(fp) = (u16)(FCUR(fp) + 1);
        if ((FF(fp) & F_LBUF) && (DSB(chp) == '\n' || DSB(chp) == '\r'))
            if (rtl_fflush(fp)) return -1;
        return DSB(chp);
    }
    if (openfd_of(fp) & 0x800) rtl_lseek(FFD(fp), 0, 2);
    if (DSB(chp) == '\n' && !(FF(fp) & F_BIN)) {
        if (rtl__write(FFD(fp), RL(cr_str), 1) != 1) goto werr;
    }
    if (rtl__write(FFD(fp), chp, 1) != 1) goto werr;
    return DSB(chp);
werr:
    if (FF(fp) & F_TERM) return DSB(chp);
err:
    FF(fp) |= F_ERR;
    return -1;
}

/* INTRO 0000:2a69 / PS 0000:378f / AP 0000:4035 _fputc: undoes putc's level increment */
s16 rtl__fputc(s16 c, u16 fp)
{
    FL(fp) = (u16)(FL(fp) - 1);
    return rtl_fputc((s16)(s8)(u8)c, fp);
}

/* INTRO 0000:2bb8 / PS 0000:38de / AP 0000:4184 fputchar */
s16 rtl_fputchar(s16 c) { return rtl_fputc(c, stream(1)); }

/* INTRO 0000:2bca / PS 0000:38f0 / AP 0000:4196 __fputn(fp, n, s): n or 0 */
u16 rtl___fputn(u16 fp, u16 n, u16 s)
{
    u16 total = n;
    if (FF(fp) & F_LBUF) {
        while (n--) if (rtl_fputc((s16)(s8)DSB(s++), fp) == -1) return 0;
        return total;
    }
    if (FF(fp) & F_BIN) {
        if (FBS(fp)) {
            if (FBS(fp) < n) {
                if (FL(fp) != 0 && rtl_fflush(fp)) return 0;
                if (openfd_of(fp) & 0x800) rtl_lseek(FFD(fp), 0, 2);
                return (u16)rtl__write(FFD(fp), s, n) == n ? total : 0;
            }
            if ((s16)(FL(fp) + n) >= 0) {
                if (FL(fp) == 0) FL(fp) = (u16)(0xFFFF - FBS(fp));
                else if (rtl_fflush(fp)) return 0;
            }
            rtl_memcpy(FCUR(fp), s, n);
            FL(fp) = (u16)(FL(fp) + n);
            FCUR(fp) = (u16)(FCUR(fp) + n);
            return total;
        }
        if (openfd_of(fp) & 0x800) rtl_lseek(FFD(fp), 0, 2);
        return (u16)rtl__write(FFD(fp), s, n) == n ? total : 0;
    }
    if (FBS(fp)) {
        while (n--) {
            s16 r;
            FL(fp) = (u16)(FL(fp) + 1);
            if (FLS(fp) < 0) {
                u8 c = DSB(s++);
                DSB(FCUR(fp)) = c;
                FCUR(fp) = (u16)(FCUR(fp) + 1);
                r = c;
            } else {
                r = rtl__fputc((s16)DSB(s++), fp);
            }
            if (r == -1) return 0;
        }
        return total;
    }
    return (u16)rtl_write(FFD(fp), s, n) == n ? total : 0;
}

/* INTRO 0000:22d9 / PS 0000:2f5b / AP 0000:37e7 fputs */
s16 rtl_fputs(u16 s_ds, u16 fp)
{
    if (!s_ds) return 0;
    u16 len = ds_strlen(s_ds);
    if (len == 0) return 0;
    if (rtl___fputn(fp, len, s_ds)) return DSB((u16)(s_ds + len - 1));
    return -1;
}

/* INTRO 0000:2635 / PS 0000:32b7 / AP 0000:3b43 _fillbuf */
static s16 fill_buf(u16 fp)
{
    if (FF(fp) & F_TERM) flush_term_lines();
    FCUR(fp) = FBUF(fp);
    s16 n = rtl_read(FFD(fp), FBUF(fp), FBS(fp));
    FL(fp) = (u16)n;
    if (n > 0) { FF(fp) &= ~F_EOF; return 0; }
    if (n == 0) FF(fp) = (u16)((FF(fp) & 0xFE7F) | F_EOF);
    else { FL(fp) = 0; FF(fp) |= F_ERR; }
    return -1;
}

/* INTRO 0000:26a3 / PS 0000:3325 / AP 0000:3bb1 fgetc */
s16 rtl_fgetc(u16 fp)
{
    if (!fp) return -1;
    if (FLS(fp) > 0) {
take:
        FL(fp) = (u16)(FL(fp) - 1);
        u8 c = DSB(FCUR(fp));
        FCUR(fp) = (u16)(FCUR(fp) + 1);
        return c;
    }
    if (FLS(fp) < 0 || (FF(fp) & 0x110) || !(FF(fp) & F_READ)) goto err;
    FF(fp) |= F_IN;
    if (FBS(fp)) {
        if (fill_buf(fp) == 0) goto take;
        return -1;
    }
    u16 chp = RL(fgetc_ch);
    for (;;) {
        if (FF(fp) & F_TERM) flush_term_lines();
        s16 r = rtl_read(FFD(fp), chp, 1);
        if (r == 0) {
            if (rtl_eof(FFD(fp)) != 1) goto err;
            FF(fp) = (u16)((FF(fp) & 0xFE7F) | F_EOF);
            return -1;
        }
        if (DSB(chp) == 0x0D && !(FF(fp) & F_BIN)) continue;   /* (r = -1 falls through here too) */
        FF(fp) &= ~F_EOF;
        return DSB(chp);
    }
err:
    FF(fp) |= F_ERR;
    return -1;
}

/* INTRO 0000:268e / PS 0000:3310 / AP 0000:3b9c _fgetc: undoes getc's level decrement */
s16 rtl__fgetc(u16 fp)
{
    FL(fp) = (u16)(FL(fp) + 1);
    return rtl_fgetc(fp);
}

static s16 getc_m(u16 fp)                            /* the getc() macro */
{
    FL(fp) = (u16)(FL(fp) - 1);
    if (FLS(fp) >= 0) {
        u8 c = DSB(FCUR(fp));
        FCUR(fp) = (u16)(FCUR(fp) + 1);
        return c;
    }
    return rtl__fgetc(fp);
}

/* INTRO 0000:2027 fgets */
u16 rtl_fgets(u16 buf_ds, s16 n, u16 fp)
{
    s16 c = 0;
    u16 di = buf_ds;
    for (;;) {
        if (c == '\n') break;
        if (--n <= 0) break;
        c = getc_m(fp);
        if (c == -1) break;
        DSB(di++) = (u8)c;
    }
    if (c == -1 && di == buf_ds) return 0;
    DSB(di) = 0;
    if (FF(fp) & F_ERR) return 0;
    return buf_ds;
}

/* INTRO 0000:2312 / PS 0000:2f94 / AP 0000:3820 _fread helper: returns the count NOT read */
static u16 fread_core(u16 fp, u16 n, u16 buf)
{
    s16 dx = 0;
    while (n != 0) {
        n++;
        u16 di = FBS(fp) > n ? n : FBS(fp);
        if ((FF(fp) & F_BIN) && FBS(fp) && FBS(fp) < n && FL(fp) == 0) {
            n--;
            di = 0;
            while (FBS(fp) <= n) { di = (u16)(di + FBS(fp)); n = (u16)(n - FBS(fp)); }
            s16 r = rtl__read(FFD(fp), buf, di);
            buf = (u16)(buf + r);
            if ((u16)r == di) continue;
            n = (u16)(n + di - r);
            FF(fp) |= F_EOF;
            return n;
        }
        for (;;) {
            n--;
            if (n == 0) break;
            di--;
            if (di == 0) break;
            dx = getc_m(fp);
            if (dx == -1) break;
            DSB(buf++) = (u8)dx;
        }
        if (dx == -1) { FF(fp) |= F_EOF; return n; }
    }
    return n;
}

/* INTRO 0000:23e9 / PS 0000:306b / AP 0000:38f7 fread */
u16 rtl_fread(u16 buf_ds, u16 size, u16 n, u16 fp)
{
    if (size == 0) return 0;
    u32 total = (u32)size * n;
    if (total >= 0x10000) return 0;
    u16 rem = fread_core(fp, (u16)total, buf_ds);
    return (u16)(((u16)total - rem) / size);
}

/* INTRO 0000:25c0 / PS 0000:3242 / AP 0000:3ace fwrite */
u16 rtl_fwrite(u16 buf_ds, u16 size, u16 n, u16 fp)
{
    if (size == 0) return n;
    u32 total = (u32)size * n;
    if (total >= 0x10000) return 0;
    return (u16)(rtl___fputn(fp, (u16)total, buf_ds) / size);
}

/* INTRO 0000:2434 / PS 0000:30b6 / AP 0000:3942 _fcount: buffered bytes (text: LF counts twice) */
static s16 fcount(u16 fp)
{
    u16 dx;
    if (FLS(fp) < 0) dx = (u16)(FBS(fp) + FL(fp) + 1);
    else dx = FL(fp);                                /* abs(level), level >= 0 */
    u16 di = dx;
    if (!(FF(fp) & F_BIN)) {
        u16 cx = FCUR(fp);
        if (FLS(fp) < 0) { while (dx--) { cx--; if (DSB(cx) == '\n') di++; } }
        else { while (dx--) { if (DSB(cx) == '\n') di++; cx++; } }
    }
    return (s16)di;
}

/* INTRO 0000:2491 / PS 0000:3113 / AP 0000:399f fseek */
s16 rtl_fseek(u16 fp, s32 offset, s16 whence)
{
    if (rtl_fflush(fp)) return -1;
    if (whence == 1 && FLS(fp) > 0) offset -= fcount(fp);
    FF(fp) &= 0xFE5F;
    FL(fp) = 0;
    FCUR(fp) = FBUF(fp);
    if (rtl_lseek(FFD(fp), offset, whence) == -1) return -1;
    return 0;
}

/* INTRO 0000:24f9 / PS 0000:317b / AP 0000:3a07 ftell */
s32 rtl_ftell(u16 fp)
{
    s32 pos = rtl_lseek(FFD(fp), 0, 1);
    if (pos == -1) return -1;
    if (FLS(fp) < 0) {
        if (openfd_of(fp) & 0x800) {
            s32 end = rtl_lseek(FFD(fp), 0, 2);
            if (end == -1) return -1;
            if (rtl_lseek(FFD(fp), pos, 0) == -1) return -1;
            pos = end;
        }
        return pos + fcount(fp);
    }
    return pos - fcount(fp);
}

/* INTRO 0000:2e43 / AP 0000:461f rewind */
void rtl_rewind(u16 fp)
{
    if (rtl_fseek(fp, 0, 0) == 0) FF(fp) &= ~F_ERR;
}

/* INTRO 0000:2089 / PS 0000:2d0b / AP 0000:3597 findfirst(path, ffblk, attrib) */
s16 rtl_findfirst(u16 path_ds, u16 ffblk_ds, s16 attrib)
{
    u16 ax;
    if (!dos_findfirst(path_ds, (u16)attrib, ffblk_ds, &ax)) return rtl_IOerror((s16)ax);
    return 0;
}

/* INTRO 0000:20bc / PS 0000:2d3e / AP 0000:35ca findnext */
s16 rtl_findnext(u16 ffblk_ds)
{
    u16 ax;
    if (!dos_findnext(ffblk_ds, &ax)) return rtl_IOerror((s16)ax);
    return 0;
}

/* ------------------------------------------------------------------ start-up and exit */

/* INTRO 0000:1199 / PS 0000:1e5d / AP 0000:227a _setupio (#pragma startup, priority 2) */
void rtl_setupio(void)
{
    u16 n = DSW(RL(nfile));
    for (u16 i = 5; i < n; i++) {
        DSW((u16)(RL(openfd) + 2 * i)) = 0;
        DSB((u16)(stream(i) + FILE_fd)) = 0xFF;
        FTOK(stream(i)) = stream(i);
    }
    if (!rtl_isatty(FFD(stream(0)))) FF(stream(0)) &= ~F_TERM;
    rtl_setvbuf(stream(0), 0, (FF(stream(0)) & F_TERM) ? 1 : 0, 0x200);
    if (!rtl_isatty(FFD(stream(1)))) FF(stream(1)) &= ~F_TERM;
    rtl_setvbuf(stream(1), 0, (FF(stream(1)) & F_TERM) ? 2 : 0, 0x200);
}

/* exit(): (*_exitbuf)() = _xfflush (the first 4 streams), (*_exitfopen)() = _xfclose (every stream);
 * _exitopen is never set. Then DOS closes the remaining handles (INT 21h/4Ch). */
void rtl_exit_streams(void)
{
    FarPtr eb = ds_far(RL(exitbuf)), ef = ds_far(RL(exitfopen));
    if (eb.off == RTL_XFFLUSH_OFF) {                  /* INTRO 0000:3867 / PS 0000:4729 / AP 0000:4a98 */
        for (int i = 0; i < 4; i++)
            if (FF(stream(i)) & F_RDWR) rtl_fflush(stream(i));
    }
    if (ef.off == RTL_XFCLOSE_OFF) {                  /* INTRO 0000:383f / PS 0000:4701 / AP 0000:4a70 */
        u16 n = DSW(RL(nfile));
        for (u16 i = 0; i < n; i++)
            if (FF(stream(i)) & F_RDWR) rtl_fclose(stream(i));
    }
    dos_close_all();
}

/* ------------------------------------------------------------------ far memory and strings */

/* PS 0000:474c / AP 0000:4abb _fmemcpy (REP MOVSW + byte, offsets wrap in their segments) */
FarPtr rtl__fmemcpy(FarPtr dst, FarPtr src, u16 n)
{
    u16 di = dst.off, si = src.off;
    for (u16 i = n >> 1; i; i--) {
        wr16(dst.seg, di, rd16(src.seg, si));
        di = (u16)(di + 2);
        si = (u16)(si + 2);
    }
    if (n & 1) wr8(dst.seg, di, rd8(src.seg, si));
    return dst;
}

/* PS 0000:4794 / AP 0000:4b03 _fmemset */
FarPtr rtl__fmemset(FarPtr dst, s16 c, u16 n)
{
    for (u16 i = 0; i < n; i++) wr8(dst.seg, (u16)(dst.off + i), (u8)c);
    return dst;
}

static u16 fstrlen(FarPtr s)
{
    u16 n = 0;
    while (rd8(s.seg, (u16)(s.off + n))) n++;
    return n;
}

/* PS 0000:4899 / AP 0000:4bfb _fstrlen (a NULL pointer gives 0) */
u16 rtl__fstrlen(FarPtr s)
{
    if (far_is_null(s)) return 0;
    return fstrlen(s);
}

/* INTRO 0000:388a / PS 0000:482f / AP 0000:4b91 _fstrcpy */
FarPtr rtl__fstrcpy(FarPtr dst, FarPtr src)
{
    u16 n = (u16)(fstrlen(src) + 1);
    for (u16 i = 0; i < n; i++) wr8(dst.seg, (u16)(dst.off + i), rd8(src.seg, (u16)(src.off + i)));
    return dst;
}

/* PS 0000:47b3 / AP 0000:4b22 _fstrcat */
FarPtr rtl__fstrcat(FarPtr dst, FarPtr src)
{
    FarPtr end = far_add(dst, fstrlen(dst));
    rtl__fstrcpy(end, src);
    return dst;
}

/* PS 0000:47f2 _fstrchr */
FarPtr rtl__fstrchr(FarPtr s, s16 c)
{
    for (u16 o = s.off;; o++) {
        u8 b = rd8(s.seg, o);
        if (b == (u8)c) return far_make(s.seg, o);
        if (!b) return far_make(0, 0);
    }
}

/* AP 0000:4b61 _fstrcmp */
s16 rtl__fstrcmp(FarPtr a, FarPtr b)
{
    u16 n = (u16)(fstrlen(b) + 1), i = 0;
    u8 x, y;
    do { x = rd8(a.seg, (u16)(a.off + i)); y = rd8(b.seg, (u16)(b.off + i)); i++; } while (--n && x == y);
    return (s16)(x - y);
}

/* PS 0000:4858 / AP 0000:4bba _fstricmp */
s16 rtl__fstricmp(FarPtr a, FarPtr b)
{
    u16 si = a.off, di = b.off;
    for (;;) {
        u8 al = rd8(a.seg, si++);
        u8 bl = rd8(b.seg, di);
        if (!al) return (s16)(al - bl);
        u8 bb = rd8(b.seg, di++);
        if (al == bb) continue;
        if (al >= 'a' && al <= 'z') al = (u8)(al - 0x20);
        if (bl >= 'a' && bl <= 'z') bl = (u8)(bl - 0x20);
        if (al != bl) return (s16)(al - bl);
    }
}

/* INTRO 0000:38b3 / PS 0000:48b8 / AP 0000:4c1a _fstrncpy */
FarPtr rtl__fstrncpy(FarPtr dst, FarPtr src, u16 n)
{
    u16 k = 0;
    while (k < n) { u8 c = rd8(src.seg, (u16)(src.off + k)); k++; if (!c) break; }
    u16 i = 0;
    for (; i < k; i++) wr8(dst.seg, (u16)(dst.off + i), rd8(src.seg, (u16)(src.off + i)));
    for (; i < n; i++) wr8(dst.seg, (u16)(dst.off + i), 0);
    return dst;
}
