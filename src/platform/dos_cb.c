/* Thin RTL wrappers in segment 0000 (the far callbacks of the video driver's DS:1A04 table, which the
 * VGA driver never calls, plus near-heap helpers game code calls directly), and the huge DOS read.
 * Addresses per program; the wrappers call the ported RTL. */
#include "platform/rtl_priv.h"



/* ---- the file wrappers (INTRO 0000:02c8.. / PS 0000:0352.. / AP 0000:02f6.., 16f9..) */

/* INTRO 0000:02c8 / AP 0000:02f6 (and AP 0000:16f9, PS 0000:0352): open(path, access) */
s16 lib_open_intro_ap_0000_02c8(u16 path_ds, u16 access) { return rtl_open(path_ds, access, 0u); }
s16 ap_lib_open_0000_16f9(u16 path_ds, u16 access) { return rtl_open(path_ds, access, 0u); }
s16 ps_lib_open_0000_0352(u16 path_ds, u16 access) { return rtl_open(path_ds, access, 0u); }
/* INTRO 0000:02db / AP 0000:0309 (and AP 0000:170c, PS 0000:0366): open(path, access, mode) */
s16 lib_open_intro_ap_0000_02db(u16 path_ds, u16 access, u16 mode) { return rtl_open(path_ds, access, (unsigned)mode); }
s16 ap_lib_open_0000_170c(u16 path_ds, u16 access, u16 mode) { return rtl_open(path_ds, access, (unsigned)mode); }
s16 ps_lib_open_0000_0366(u16 path_ds, u16 access, u16 mode) { return rtl_open(path_ds, access, (unsigned)mode); }
/* INTRO 0000:02f1 / AP 0000:1722 (and AP 0000:031f, PS 0000:037e): close */
s16 lib_close(s16 fd) { return rtl_close(fd); }
s16 ap_lib_close(s16 fd) { return rtl_close(fd); }
s16 ps_lib_close(s16 fd) { return rtl_close(fd); }
/* INTRO 0000:0300 / PS 0000:038e / AP 0000:032f, 0000:1731 ("remove"): read(fd, buf, n) */
s16 intro_lib_remove(s16 fd, u16 buf_ds, u16 n) { return rtl_read(fd, buf_ds, n); }
s16 ps_lib_remove(s16 fd, u16 buf_ds, u16 n) { return rtl_read(fd, buf_ds, n); }
s16 ap_lib_remove_0000_032f(s16 fd, u16 buf_ds, u16 n) { return rtl_read(fd, buf_ds, n); }
s16 ap_lib_remove_0000_1731(s16 fd, u16 buf_ds, u16 n) { return rtl_read(fd, buf_ds, n); }
/* PS 0000:03a6 / AP 0000:0345: lseek */
s32 ps_lib_lseek(s16 fd, s32 off, s16 whence) { return rtl_lseek(fd, off, whence); }
s32 ap_lib_lseek(s16 fd, s32 off, s16 whence) { return rtl_lseek(fd, off, whence); }
/* INTRO 0000:0316 / AP 0000:1747: fopen */
u16 intro_lib_fopen(u16 path_ds, u16 mode_ds) { return rtl_fopen(path_ds, mode_ds); }
u16 ap_lib_fopen(u16 path_ds, u16 mode_ds) { return rtl_fopen(path_ds, mode_ds); }
/* INTRO 0000:0329 / AP 0000:175a: fseek */
s16 lib_fseek(u16 fp, s32 off, s16 whence) { return rtl_fseek(fp, off, whence); }
/* INTRO 0000:0342 / AP 0000:1773: ftell */
s32 intro_lib_ftell(u16 fp) { return rtl_ftell(fp); }
s32 ap_lib_ftell(u16 fp) { return rtl_ftell(fp); }
/* INTRO 0000:0351 / AP 0000:1782: fread */
u16 intro_lib_fread(u16 buf, u16 size, u16 n, u16 fp) { return rtl_fread(buf, size, n, fp); }
u16 ap_lib_fread(u16 buf, u16 size, u16 n, u16 fp) { return rtl_fread(buf, size, n, fp); }
/* INTRO 0000:036a / AP 0000:179b: fwrite */
u16 intro_lib_fwrite(u16 buf, u16 size, u16 n, u16 fp) { return rtl_fwrite(buf, size, n, fp); }
u16 ap_lib_fwrite(u16 buf, u16 size, u16 n, u16 fp) { return rtl_fwrite(buf, size, n, fp); }
/* INTRO 0000:0383 / AP 0000:17b4: fputc */
s16 intro_lib_fputc(s16 c, u16 fp) { return rtl_fputc(c, fp); }
s16 ap_lib_fputc(s16 c, u16 fp) { return rtl_fputc(c, fp); }
/* INTRO 0000:0396 / AP 0000:17c7: rewind */
void intro_lib_rewind(u16 fp) { rtl_rewind(fp); }
void ap_lib_rewind(u16 fp) { rtl_rewind(fp); }
/* INTRO 0000:03a5 / AP 0000:17d6: fclose */
s16 intro_lib_fclose(u16 fp) { return rtl_fclose(fp); }
s16 ap_lib_fclose(u16 fp) { return rtl_fclose(fp); }
/* INTRO 0000:041e / AP 0000:184f: fgetc */
s16 intro_lib_fgetc(u16 fp) { return rtl_fgetc(fp); }
s16 ap_lib_fgetc(u16 fp) { return rtl_fgetc(fp); }

/* ---- heap */

/* INTRO 0000:03b4 / AP 0000:17e5: malloc */
u16 intro_lib_malloc(u16 n) { return rtl_malloc(n); }
u16 ap_lib_malloc(u16 n) { return rtl_malloc(n); }
/* INTRO 0000:03c3 / AP 0000:17f4 / PS 0000:0454 / AP 0000:03ac: free */
void intro_lib_free(u16 p) { rtl_free(p); }
void ap_lib_free(u16 p) { rtl_free(p); }
void lib_free(u16 p) { rtl_free(p); }
/* INTRO 0000:040b / AP 0000:183c: calloc */
u16 intro_lib_calloc(u16 n, u16 size) { return rtl_calloc(n, size); }
u16 ap_lib_calloc(u16 n, u16 size) { return rtl_calloc(n, size); }

/* PS 0000:03c1 (listed as lib_realloc): PS's safe_malloc(n) - malloc, then give the block back if
 * it reaches into the top _stklen bytes of DGROUP (p + n wraps as 16 bits, as the original). */
u16 lib_realloc(u16 n)
{
    u16 p = rtl_malloc(n);
    if ((u16)(p + n) > (u16)(0xFFFF - LW(G__stklen))) { rtl_free(p); p = 0; }
    return p;
}

/* PS 0000:0404: calloc with the same stack guard */
u16 ps_lib_calloc(u16 n, u16 size)
{
    u16 p = rtl_calloc(n, size);
    if ((u16)(p + (u16)(size * n)) > (u16)(0xFFFF - LW(G__stklen))) { rtl_free(p); p = 0; }
    return p;
}

/* ---- string helpers (inline string code in the wrappers) */

/* INTRO 0000:03d2 / PS 0000:0462 / AP 0000:03bc, 0000:1803: strcat */
u16 intro_lib_strcat(u16 d, u16 s) { return rtl_strcat(d, s); }
u16 ps_lib_strcat(u16 d, u16 s) { return rtl_strcat(d, s); }
u16 ap_lib_strcat_0000_03bc(u16 d, u16 s) { return rtl_strcat(d, s); }
u16 ap_lib_strcat_0000_1803(u16 d, u16 s) { return rtl_strcat(d, s); }
/* INTRO 0000:03e5 / PS 0000:0476 / AP 0000:03f2, 0000:1816: strcpy */
void intro_lib_strcpy(u16 d, u16 s) { rtl_strcpy(d, s); }
void ps_lib_strcpy(u16 d, u16 s) { rtl_strcpy(d, s); }
void ap_lib_strcpy_0000_03f2(u16 d, u16 s) { rtl_strcpy(d, s); }
u16 ap_lib_strcpy_0000_1816(u16 d, u16 s) { return rtl_strcpy(d, s); }
/* INTRO 0000:03f8 / PS 0000:048a / AP 0000:0420, 0000:1829: strchr (scans strlen+1 bytes) */
u16 intro_lib_strchr(u16 s, s16 c) { return rtl_strchr(s, c); }
u16 ps_lib_strchr(u16 s, s16 c) { return rtl_strchr(s, c); }
u16 ap_lib_strchr_0000_0420(u16 s, s16 c) { return rtl_strchr(s, c); }
u16 ap_lib_strchr_0000_1829(u16 s, s16 c) { return rtl_strchr(s, c); }

/* ---- huge DOS read */

/* INTRO 0000:043b / PS 0000:02c8 / AP 0000:1694 __hread(buf, n, fd): INT 21h/3Fh in pieces of
 * 7FFFh bytes, the pointer renormalised per piece; returns n (even after a short read!) or 0 when
 * DOS reports an error. (INTRO 0000:042d hread_far = the entry with AH = 3Fh preset.) */
u32 lib_hread(FarPtr buf, u32 n, s16 fd)
{
    u32 lin = far_lin(buf);
    u32 left = n;
    u16 ax;
    while (left > 0x7FFF) {
        if (!dos_read((u16)fd, far_norm(lin), 0x7FFF, &ax)) return 0;
        lin += 0x7FFF;
        left -= 0x7FFF;
    }
    if (!dos_read((u16)fd, far_norm(lin), (u16)left, &ax)) return 0;
    return n;
}

u32 hread_far(FarPtr buf, u32 n, s16 fd) { return lib_hread(buf, n, fd); }

/* AP 0000:18d6 floppy_motor_off(DX = delay factor): BIOS motor timeout 40:40 = 1, then wait (up to
 * F000h rounds) until the motor bits of 40:3F clear; returns the rounds left (CX). */
u16 floppy_motor_off(u16 dx)
{
    (void)dx;                                        /* only the delay IMULs use it */
    wr8(0x40, 0x40, 1);
    u16 cx = 0xF000;
    do { cx--; } while (cx && (rd8(0x40, 0x3F) & 3));
    return cx;
}
