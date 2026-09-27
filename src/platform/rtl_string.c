/* Borland C++ 3.0 RTL: near string and memory functions (medium model: DS offsets, 16-bit offset
 * arithmetic that wraps inside DGROUP). Addresses INTRO / PS / AP. */
#include "platform/rtl_priv.h"

#include <string.h>

/* Word copy as REP MOVSW (+ odd byte), forward, offsets wrapping inside their segments. */
static void movs(u16 dseg, u16 di, u16 sseg, u16 si, u16 n)
{
    for (u16 i = n >> 1; i; i--) {
        u16 w = rd16(sseg, si);
        wr16(dseg, di, w);
        si = (u16)(si + 2);
        di = (u16)(di + 2);
    }
    if (n & 1) wr8(dseg, di, rd8(sseg, si));
}

/* INTRO 0000:2822 / PS 0000:34c3 / AP 0000:3d69 memcpy */
u16 rtl_memcpy(u16 dst_ds, u16 src_ds, u16 n)
{
    movs(cur_dgroup, dst_ds, cur_dgroup, src_ds, n);
    return dst_ds;
}

/* INTRO 0000:2841 / PS 0000:34e2 / AP 0000:3d88 setmem(dst, n, c) */
u16 rtl_setmem_ds(u16 dst_ds, u16 n, u8 c)
{
    for (u16 i = 0; i < n; i++) DSB((u16)(dst_ds + i)) = c;
    return dst_ds;
}

/* INTRO 0000:2867 / PS 0000:3508 / AP 0000:3dae memset */
u16 rtl_memset(u16 dst_ds, s16 c, u16 n)
{
    rtl_setmem_ds(dst_ds, n, (u8)c);
    return dst_ds;
}

/* PS 0000:3545 / AP 0000:3deb (movmem) + PS 0000:358e / AP 0000:3e34 memmove: overlap-safe. */
u16 rtl_memmove(u16 dst_ds, u16 src_ds, u16 n)
{
    if (n == 0) return dst_ds;
    u8 tmp[65536];
    for (u16 i = 0; i < n; i++) tmp[i] = DSB((u16)(src_ds + i));
    for (u16 i = 0; i < n; i++) DSB((u16)(dst_ds + i)) = tmp[i];
    return dst_ds;
}

/* PS 0000:3523 / AP 0000:3dc9 movedata(srcseg, srcoff, dstseg, dstoff, n) */
void rtl_movedata(u16 srcseg, u16 srcoff, u16 dstseg, u16 dstoff, u16 n)
{
    movs(dstseg, dstoff, srcseg, srcoff, n);
}

/* PS 0000:34a4 memchr */
u16 rtl_memchr(u16 s_ds, s16 c, u16 n)
{
    for (u16 i = 0; i < n; i++)
        if (DSB((u16)(s_ds + i)) == (u8)c) return (u16)(s_ds + i);
    return 0;
}

/* INTRO 0000:30a3 / PS 0000:3f95 / AP 0000:484b strlen */
u16 rtl_strlen(u16 s_ds) { return ds_strlen(s_ds); }

/* INTRO 0000:3041 / PS 0000:3f33 / AP 0000:47e9 strcpy */
u16 rtl_strcpy(u16 dst_ds, u16 src_ds)
{
    u16 n = (u16)(ds_strlen(src_ds) + 1);
    for (u16 i = 0; i < n; i++) DSB((u16)(dst_ds + i)) = DSB((u16)(src_ds + i));   /* REP MOVSB */
    return dst_ds;
}

/* INTRO 0000:17ea / PS 0000:24da / AP 0000:2846 stpcpy: strcpy, returns the end */
u16 rtl_stpcpy(u16 dst_ds, u16 src_ds)
{
    u16 n = ds_strlen(src_ds);
    rtl_memcpy(dst_ds, src_ds, (u16)(n + 1));
    return (u16)(dst_ds + n);
}

/* INTRO 0000:2fa3 / PS 0000:3e95 / AP 0000:477a strcat */
u16 rtl_strcat(u16 dst_ds, u16 src_ds)
{
    u16 end = (u16)(dst_ds + ds_strlen(dst_ds));
    u16 n = (u16)(ds_strlen(src_ds) + 1);
    for (u16 i = 0; i < n; i++) DSB((u16)(end + i)) = DSB((u16)(src_ds + i));
    return dst_ds;
}

/* INTRO 0000:2fdc / PS 0000:3ece / AP 0000:47b3 strchr (c = 0 finds the terminator) */
u16 rtl_strchr(u16 s_ds, s16 c)
{
    for (u16 p = s_ds;; p++) {
        u8 b = DSB(p);
        if (b == (u8)c) return p;
        if (!b) return 0;
    }
}

/* INTRO 0000:30e9 / PS 0000:3fdb strrchr (scans strlen+1 bytes from the end) */
u16 rtl_strrchr(u16 s_ds, s16 c)
{
    u16 n = (u16)(ds_strlen(s_ds) + 1);
    u16 p = (u16)(s_ds + n);
    for (; n; n--) {
        p--;
        if (DSB(p) == (u8)c) return p;
    }
    return 0;
}

/* INTRO 0000:3012 / PS 0000:3f04 strcmp: REPE CMPSB over strlen(b)+1 bytes, AL - BL */
s16 rtl_strcmp(u16 a_ds, u16 b_ds)
{
    u16 n = (u16)(ds_strlen(b_ds) + 1);
    u16 i = 0;
    u8 x, y;
    do { x = DSB((u16)(a_ds + i)); y = DSB((u16)(b_ds + i)); i++; } while (--n && x == y);
    return (s16)(x - y);
}

static u8 fold_up(u8 c) { return (c >= 'a' && c <= 'z') ? (u8)(c - 0x20) : c; }

/* INTRO 0000:3063 / PS 0000:3f55 / AP 0000:480b stricmp */
s16 rtl_stricmp(u16 a_ds, u16 b_ds)
{
    u16 si = a_ds, di = b_ds;
    for (;;) {
        u8 al = DSB(si++);
        u8 bl = DSB(di);
        if (!al) return (s16)(al - bl);
        u8 b = DSB(di++);
        if (al == b) continue;
        al = fold_up(al);
        bl = fold_up(bl);
        if (al != bl) return (s16)(al - bl);
    }
}

/* AP 0000:4865 strnicmp */
s16 rtl_strnicmp(u16 a_ds, u16 b_ds, u16 n)
{
    u16 si = a_ds, di = b_ds, cx = n;
    u8 al = 0, bl = 0;
    for (;;) {
        if (cx == 0) return (s16)(al - bl);
        al = DSB(si++);
        bl = DSB(di);
        if (!al) return (s16)(al - bl);
        u8 b = DSB(di++);
        cx--;
        if (al == b) continue;                       /* LOOPE: continues while equal and cx != 0 */
        al = fold_up(al);
        bl = fold_up(bl);
        if (al != bl) return (s16)(al - bl);
    }
}

/* INTRO 0000:30bd / PS 0000:3faf strncpy: copies up to n bytes, pads with zeros */
u16 rtl_strncpy(u16 dst_ds, u16 src_ds, u16 n)
{
    u16 k = 0;
    while (k < n) { u8 c = DSB((u16)(src_ds + k)); k++; if (!c) break; }   /* REPNE SCASB */
    u16 i = 0;
    for (; i < k; i++) DSB((u16)(dst_ds + i)) = DSB((u16)(src_ds + i));
    for (; i < n; i++) DSB((u16)(dst_ds + i)) = 0;
    return dst_ds;
}

/* PS 0000:400a strrev */
u16 rtl_strrev(u16 s_ds)
{
    u16 n = ds_strlen(s_ds);
    if (n == 0) return s_ds;
    u16 i = s_ds, j = (u16)(s_ds + n - 1);
    while (i < j) {
        u8 t = DSB(i);
        DSB(i) = DSB(j);
        DSB(j) = t;
        i++;
        j--;
    }
    return s_ds;
}

/* INTRO 0000:3118 / AP 0000:48a8 strstr */
u16 rtl_strstr(u16 s_ds, u16 sub_ds)
{
    u16 ln = ds_strlen(sub_ds);
    if (ln == 0) return s_ds;
    u16 ls = ds_strlen(s_ds);
    for (u16 i = 0; i + ln <= ls; i++) {
        u16 k = 0;
        while (k < ln && DSB((u16)(s_ds + i + k)) == DSB((u16)(sub_ds + k))) k++;
        if (k == ln) return (u16)(s_ds + i);
    }
    return 0;
}

/* INTRO 0000:3178 / PS 0000:403a / AP 0000:4908 strupr */
u16 rtl_strupr(u16 s_ds)
{
    for (u16 p = s_ds;; p++) {
        u8 c = DSB(p);
        if (!c) return s_ds;
        if ((u8)(c - 'a') <= 0x19) DSB(p) = (u8)(c - 'a' + 'A');
    }
}

/* INTRO 0000:12dd / PS 0000:1fa1 tolower: _ctype[c] & _IS_UPP -> +20h (on the low byte of c) */
s16 rtl_tolower(s16 c)
{
    if (c == -1) return -1;
    u8 b = (u8)c;
    if (DSB((u16)(RL(ctype1) + b)) & 4) return (s16)(b + 0x20);
    return b;
}

/* PS 0000:1fcd / AP 0000:2339 toupper: _ctype[c] & _IS_LOW -> -20h */
s16 rtl_toupper(s16 c)
{
    if (c == -1) return -1;
    u8 b = (u8)c;
    if (DSB((u16)(RL(ctype1) + b)) & 8) return (s16)(u16)(b - 0x20);
    return b;
}
