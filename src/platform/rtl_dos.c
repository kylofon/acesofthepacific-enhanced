/* Borland C++ 3.0 RTL, DOS layer (medium model): open/close/read/write/lseek on DOS handles, the
 * error helpers, and the INT 21h file functions they use, modelled on host files.
 *
 * The DOS work directory of the programs is the virtual path C:\DYNAMIX\AOTP (prog.h DOS_WORK_DIR);
 * the current directory is the same place. dos_host_path() maps "C:\DYNAMIX\AOTP\X", "\X" (the root
 * quirk of a missing -n, platform_resources.md §6.1.6) and relative names to the game folder, each
 * component looked up case-insensitively (DOS names). New files are created with upper-case names,
 * as DOS does. Handles 0..4 are the standard devices (CON for 0-2, AUX, PRN); text written to 1/2
 * goes to the host's stderr (PORTING.md: text mode is not displayed). PORT: the DOS system file table
 * is host-side (open FILE* per handle); nothing in the programs reads it. */
#include "platform/rtl_priv.h"

#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <unistd.h>

#include "host.h"
#include "platform/vga.h"
#include "prog.h"

/* ------------------------------------------------------------------ path mapping */

static bool ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
    return *a == *b;
}

/* Looks up one name case-insensitively in host directory dir; returns a malloc'd "dir/entry" or NULL. */
static char *find_in_dir(const char *dir, const char *name)
{
    DIR *d = opendir(dir);
    if (!d) return NULL;
    char *res = NULL;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (ieq(e->d_name, name)) {
            size_t n = strlen(dir) + strlen(e->d_name) + 2;
            res = malloc(n);
            snprintf(res, n, "%s/%s", dir, e->d_name);
            break;
        }
    }
    closedir(d);
    return res;
}

static bool is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static bool is_file(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }

/* Splits a DOS path into components relative to the work directory. Returns the number of
 * components (at most 8) or -1 if the path leaves the game folder. */
static int dos_components(const char *dos, char comp[8][64])
{
    char buf[256];
    size_t n = 0;
    for (const char *p = dos; *p && n < sizeof buf - 1; p++) buf[n++] = *p == '/' ? '\\' : *p;
    buf[n] = 0;
    const char *p = buf;
    if (isalpha((unsigned char)p[0]) && p[1] == ':') p += 2;     /* drive: every drive is C: */
    static const char work[] = "\\DYNAMIX\\AOTP";
    if (*p == '\\') {
        size_t wl = strlen(work);
        bool under = strlen(p) >= wl && (p[wl] == '\\' || p[wl] == 0);
        for (size_t i = 0; under && i < wl; i++)
            if (toupper((unsigned char)p[i]) != work[i]) under = false;
        p += under ? wl : 0;       /* PORT: any other absolute path ("\X": no -n) = the work dir too */
    }
    int nc = 0;
    while (*p) {
        while (*p == '\\') p++;
        if (!*p) break;
        char c[64];
        size_t l = 0;
        while (*p && *p != '\\') { if (l < sizeof c - 1) c[l++] = *p; p++; }
        c[l] = 0;
        if (!strcmp(c, ".")) continue;
        if (!strcmp(c, "..")) { if (nc > 0) nc--; continue; }
        if (nc == 8) return -1;
        memcpy(comp[nc++], c, l + 1);
    }
    return nc;
}

char *dos_host_path(const char *dos_name, bool create)
{
    char comp[8][64];
    int nc = dos_components(dos_name, comp);
    if (nc <= 0) return NULL;
    const char *root = host_game_dir();
    char *cur = malloc(strlen(root) + 1);
    strcpy(cur, root);
    for (int i = 0; i < nc; i++) {
        char *next = find_in_dir(cur, comp[i]);
        if (!next) {
            if (!create || i != nc - 1) { free(cur); return NULL; }
            size_t n = strlen(cur) + strlen(comp[i]) + 2;
            next = malloc(n);
            snprintf(next, n, "%s/", cur);
            char *q = next + strlen(next);
            for (const char *s = comp[i]; *s; s++) *q++ = (char)toupper((unsigned char)*s);   /* DOS names */
            *q = 0;
        }
        free(cur);
        cur = next;
    }
    char *res = malloc(strlen(cur) + 1);   /* hand out host_free()-compatible memory? plain free */
    strcpy(res, cur);
    free(cur);
    return res;
}

/* ------------------------------------------------------------------ DOS handles */

#define DOS_MAX_HANDLES 20            /* the PSP job file table */

typedef struct {
    bool used;
    int dev;                          /* 0 = file, 1 = CON, 2 = AUX, 3 = PRN */
    FILE *f;
    int last_op;                      /* 0 none, 1 read, 2 write (C stdio needs a seek in between) */
} DosHandle;

static DosHandle handles[DOS_MAX_HANDLES];

void dos_close_all(void)
{
    for (int i = 0; i < DOS_MAX_HANDLES; i++) {
        if (handles[i].used && handles[i].f) fclose(handles[i].f);
        handles[i] = (DosHandle){ 0 };
    }
}

void dos_prog_start(void)
{
    dos_close_all();
    handles[0] = (DosHandle){ true, 1, NULL, 0 };
    handles[1] = (DosHandle){ true, 1, NULL, 0 };
    handles[2] = (DosHandle){ true, 1, NULL, 0 };
    handles[3] = (DosHandle){ true, 2, NULL, 0 };
    handles[4] = (DosHandle){ true, 3, NULL, 0 };
}

static int alloc_handle(void)
{
    for (int i = 0; i < DOS_MAX_HANDLES; i++)
        if (!handles[i].used) return i;
    return -1;
}

static DosHandle *get_handle(u16 h)
{
    return h < DOS_MAX_HANDLES && handles[h].used ? &handles[h] : NULL;
}

static void dos_name(u16 path_ds, char *out, size_t n)
{
    size_t i = 0;
    for (; i < n - 1 && DSB((u16)(path_ds + i)); i++) out[i] = (char)DSB((u16)(path_ds + i));
    out[i] = 0;
}

static u8 *far_buf(FarPtr p, u16 n)
{
    u32 a = far_lin(p);
    if (a >= 0xA0000 && a < 0xB0000) host_fatal("DOS transfer into video memory %04X:%04X", p.seg, p.off);
    if (a + n > MEM_SIZE) host_fatal("DOS transfer beyond memory %04X:%04X", p.seg, p.off);
    return mem + a;
}

bool dos_open(u16 path_ds, u8 mode, u16 *ax)
{
    char name[128];
    dos_name(path_ds, name, sizeof name);
    if (ieq(name, "CON") || ieq(name, "NUL") || ieq(name, "PRN") || ieq(name, "AUX")) {
        int h = alloc_handle();
        if (h < 0) { *ax = 4; return false; }
        handles[h] = (DosHandle){ true, ieq(name, "CON") ? 1 : ieq(name, "AUX") ? 2 : 3, NULL, 0 };
        *ax = (u16)h;
        return true;
    }
    char *path = dos_host_path(name, false);
    if (!path || !is_file(path)) { free(path); *ax = 2; return false; }   /* file not found */
    int h = alloc_handle();
    if (h < 0) { free(path); *ax = 4; return false; }                    /* too many open files */
    FILE *f = fopen(path, (mode & 3) == 0 ? "rb" : "r+b");
    if (!f && (mode & 3) != 0) { free(path); *ax = 5; return false; }   /* access denied */
    free(path);
    if (!f) { *ax = 2; return false; }
    handles[h] = (DosHandle){ true, 0, f, 0 };
    *ax = (u16)h;
    return true;
}

bool dos_creat(u16 path_ds, u16 attr, u16 *ax)
{
    (void)attr;                                      /* PORT: file attributes are not modelled */
    char name[128];
    dos_name(path_ds, name, sizeof name);
    char *path = dos_host_path(name, true);
    if (!path) { *ax = 3; return false; }            /* path not found */
    int h = alloc_handle();
    if (h < 0) { free(path); *ax = 4; return false; }
    FILE *f = fopen(path, "w+b");
    free(path);
    if (!f) { *ax = 5; return false; }
    handles[h] = (DosHandle){ true, 0, f, 0 };
    *ax = (u16)h;
    return true;
}

bool dos_close(u16 h, u16 *ax)
{
    DosHandle *d = get_handle(h);
    if (!d) { *ax = 6; return false; }               /* invalid handle */
    if (d->f) fclose(d->f);
    *d = (DosHandle){ 0 };
    *ax = 0;
    return true;
}

static void switch_op(DosHandle *d, int op)
{
    if (d->f && d->last_op && d->last_op != op) fseek(d->f, 0, SEEK_CUR);
    d->last_op = op;
}

bool dos_read(u16 h, FarPtr buf, u16 n, u16 *ax)
{
    DosHandle *d = get_handle(h);
    if (!d) { *ax = 6; return false; }
    if (d->dev) { *ax = 0; return true; }            /* PORT: no console input (EOF) */
    switch_op(d, 1);
    *ax = (u16)fread(far_buf(buf, n), 1, n, d->f);
    return true;
}

bool dos_write(u16 h, FarPtr buf, u16 n, u16 *ax)
{
    DosHandle *d = get_handle(h);
    if (!d) { *ax = 6; return false; }
    if (d->dev) {
        if (d->dev == 1) { fwrite(far_buf(buf, n), 1, n, stderr); fflush(stderr); }
        *ax = n;
        return true;
    }
    switch_op(d, 2);
    if (n == 0) {                                    /* CX = 0: truncate (or extend) at the position */
        fflush(d->f);
        long pos = ftell(d->f);
        if (pos < 0 || ftruncate(fileno(d->f), pos) != 0) { *ax = 5; return false; }
        *ax = 0;
        return true;
    }
    *ax = (u16)fwrite(far_buf(buf, n), 1, n, d->f);
    return true;
}

bool dos_lseek(u16 h, s32 off, u8 whence, u32 *pos, u16 *ax)
{
    DosHandle *d = get_handle(h);
    if (!d) { *ax = 6; return false; }
    if (whence > 2) { *ax = 1; return false; }
    if (d->dev) { *pos = 0; *ax = 0; return true; }
    d->last_op = 0;
    long base = whence == 0 ? 0 : whence == 1 ? ftell(d->f) : (fseek(d->f, 0, SEEK_END), ftell(d->f));
    long t = base + off;
    if (t < 0) t = (long)(u32)t;                     /* DOS takes the 32-bit position as unsigned */
    fseek(d->f, t, SEEK_SET);                        /* beyond the end is allowed (DOS extends on write) */
    *pos = (u32)t;
    *ax = (u16)t;
    return true;
}

bool dos_getattr(u16 path_ds, u16 *cx, u16 *ax)
{
    char name[128];
    dos_name(path_ds, name, sizeof name);
    char *path = dos_host_path(name, false);
    if (!path) { *ax = 2; return false; }
    bool dir = is_dir(path);
    free(path);
    *cx = dir ? 0x10 : 0x20;                         /* directory / archive */
    *ax = *cx;
    return true;
}

bool dos_unlink(u16 path_ds, u16 *ax)
{
    char name[128];
    dos_name(path_ds, name, sizeof name);
    char *path = dos_host_path(name, false);
    if (!path || !is_file(path)) { free(path); *ax = 2; return false; }
    int r = remove(path);
    free(path);
    if (r != 0) { *ax = 5; return false; }
    *ax = 0;
    return true;
}

bool dos_rename(u16 old_ds, u16 new_ds, u16 *ax)
{
    char a[128], b[128];
    dos_name(old_ds, a, sizeof a);
    dos_name(new_ds, b, sizeof b);
    char *pa = dos_host_path(a, false);
    if (!pa || !is_file(pa)) { free(pa); *ax = 2; return false; }
    char *pb = dos_host_path(b, true);
    if (!pb || is_file(pb)) { free(pa); free(pb); *ax = 5; return false; }
    int r = rename(pa, pb);
    free(pa);
    free(pb);
    if (r != 0) { *ax = 5; return false; }
    *ax = 0;
    return true;
}

bool dos_ioctl_info(u16 h, u16 *dx, u16 *ax)
{
    DosHandle *d = get_handle(h);
    if (!d) { *ax = 6; return false; }
    /* character device: bit 7; CON: stdin/stdout bits 0/1, bit 4 special; files: drive C (2) */
    *dx = d->dev == 1 ? 0x80D3 : d->dev ? 0x80C0 : 0x0002;
    *ax = *dx;
    return true;
}

bool dos_chdir(u16 path_ds, u16 *ax)
{
    char name[128];
    dos_name(path_ds, name, sizeof name);
    char *path = dos_host_path(name, false);
    bool ok = path && is_dir(path);
    free(path);
    /* PORT: the current directory stays the work directory (the programs only chdir while no
     * volume is open, never in the shipped game: platform.md §2.4). */
    *ax = ok ? 0 : 3;
    return ok;
}

/* ------------------------------------------------------------------ findfirst / findnext */

#define FIND_SLOTS 8
static struct {
    bool used;
    int n, next;
    char (*names)[13];
    u32 *sizes;
    u16 *times, *dates;
} finds[FIND_SLOTS];
static int find_rr;

/* DOS 8.3 wildcard match of an upper-case name against an upper-case pattern. */
static bool match_part(const char *p, int pl, const char *s, int sl)
{
    int i = 0, j = 0;
    for (; i < pl; i++) {
        if (p[i] == '*') return true;
        if (p[i] == '?') { if (j < sl) j++; continue; }
        if (j >= sl || p[i] != s[j]) return false;
        j++;
    }
    return j == sl;
}

static bool dos_match(const char *pat, const char *name)
{
    const char *pd = strchr(pat, '.'), *nd = strchr(name, '.');
    int pbl = pd ? (int)(pd - pat) : (int)strlen(pat), nbl = nd ? (int)(nd - name) : (int)strlen(name);
    if (!match_part(pat, pbl, name, nbl)) return false;
    const char *pe = pd ? pd + 1 : "", *ne = nd ? nd + 1 : "";
    if (!pd && !strcmp(pat + pbl, "")) return *ne == 0 || (pbl > 0 && pat[pbl - 1] == '*');
    return match_part(pe, (int)strlen(pe), ne, (int)strlen(ne));
}

static bool valid83(const char *s)
{
    const char *d = strchr(s, '.');
    size_t bl = d ? (size_t)(d - s) : strlen(s);
    if (bl == 0 || bl > 8) return false;
    if (d && (strlen(d + 1) > 3 || strchr(d + 1, '.'))) return false;
    for (; *s; s++) if ((unsigned char)*s <= ' ' || (unsigned char)*s >= 0x7F) return false;
    return true;
}

static int cmp13(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static void find_free(int s)
{
    free(finds[s].names);
    free(finds[s].sizes);
    free(finds[s].times);
    free(finds[s].dates);
    finds[s] = (typeof(finds[0])){ 0 };
}

static bool find_fill(int s, u16 dta_ds, u16 *ax)
{
    if (finds[s].next >= finds[s].n) { find_free(s); *ax = 0x12; return false; }   /* no more files */
    int i = finds[s].next++;
    DSB(dta_ds + 0x00) = 0xA5;                       /* reserved: the search state (host-side slot) */
    DSB(dta_ds + 0x01) = (u8)s;
    DSB(dta_ds + 0x15) = 0x20;
    DSW(dta_ds + 0x16) = finds[s].times[i];
    DSW(dta_ds + 0x18) = finds[s].dates[i];
    DSL(dta_ds + 0x1A) = finds[s].sizes[i];
    memset(DSP(dta_ds + 0x1E), 0, 13);
    memcpy(DSP(dta_ds + 0x1E), finds[s].names[i], strlen(finds[s].names[i]));
    *ax = 0;
    return true;
}

bool dos_findfirst(u16 path_ds, u16 attr, u16 dta_ds, u16 *ax)
{
    (void)attr;                                      /* only plain files exist in the game folder */
    char name[128];
    dos_name(path_ds, name, sizeof name);
    char *bs = strrchr(name, '\\');
    char dirpart[128], pat[20];
    if (bs) { size_t l = (size_t)(bs - name); memcpy(dirpart, name, l); dirpart[l] = 0; snprintf(pat, sizeof pat, "%s", bs + 1); }
    else { dirpart[0] = 0; snprintf(pat, sizeof pat, "%s", name[1] == ':' ? name + 2 : name); }
    for (char *p = pat; *p; p++) *p = (char)toupper((unsigned char)*p);
    char *dir = dirpart[0] ? dos_host_path(dirpart, false) : NULL;
    const char *hd = dirpart[0] ? dir : host_game_dir();
    if (!hd) { *ax = 3; return false; }
    int s = find_rr;
    find_rr = (find_rr + 1) % FIND_SLOTS;
    if (finds[s].used) find_free(s);
    finds[s].used = true;
    DIR *d = opendir(hd);
    int cap = 0;
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL) {
        char up[260];
        size_t l = strlen(e->d_name);
        if (l >= sizeof up) continue;
        for (size_t k = 0; k <= l; k++) up[k] = (char)toupper((unsigned char)e->d_name[k]);
        if (!valid83(up) || !dos_match(pat, up)) continue;
        size_t pl = strlen(hd) + l + 2;
        char *fp = malloc(pl);
        snprintf(fp, pl, "%s/%s", hd, e->d_name);
        struct stat st;
        bool ok = stat(fp, &st) == 0 && S_ISREG(st.st_mode);
        free(fp);
        if (!ok) continue;
        if (finds[s].n == cap) {
            cap = cap ? cap * 2 : 16;
            finds[s].names = realloc(finds[s].names, (size_t)cap * 13);
            finds[s].sizes = realloc(finds[s].sizes, (size_t)cap * sizeof(u32));
            finds[s].times = realloc(finds[s].times, (size_t)cap * sizeof(u16));
            finds[s].dates = realloc(finds[s].dates, (size_t)cap * sizeof(u16));
        }
        int k = finds[s].n++;
        snprintf(finds[s].names[k], 13, "%s", up);
        finds[s].sizes[k] = (u32)st.st_size;
        struct tm *tm = localtime(&st.st_mtime);
        finds[s].times[k] = tm ? (u16)(tm->tm_hour << 11 | tm->tm_min << 5 | tm->tm_sec / 2) : 0;
        finds[s].dates[k] = tm ? (u16)((tm->tm_year - 80) << 9 | (tm->tm_mon + 1) << 5 | tm->tm_mday) : 0;
    }
    if (d) closedir(d);
    free(dir);
    /* PORT: DOS returns directory order; on the CD (ISO 9660) that is alphabetical: sorted. Sizes,
     * times and dates travel with the names. */
    for (int a = 1; a < finds[s].n; a++)
        for (int b = a; b > 0 && cmp13(finds[s].names[b - 1], finds[s].names[b]) > 0; b--) {
            char t[13]; memcpy(t, finds[s].names[b], 13); memcpy(finds[s].names[b], finds[s].names[b - 1], 13); memcpy(finds[s].names[b - 1], t, 13);
            u32 z = finds[s].sizes[b]; finds[s].sizes[b] = finds[s].sizes[b - 1]; finds[s].sizes[b - 1] = z;
            u16 w = finds[s].times[b]; finds[s].times[b] = finds[s].times[b - 1]; finds[s].times[b - 1] = w;
            w = finds[s].dates[b]; finds[s].dates[b] = finds[s].dates[b - 1]; finds[s].dates[b - 1] = w;
        }
    if (finds[s].n == 0) { find_free(s); *ax = 0x12; return false; }   /* DOS: 12h no more files */
    return find_fill(s, dta_ds, ax);
}

bool dos_findnext(u16 dta_ds, u16 *ax)
{
    int s = DSB(dta_ds + 1);
    if (DSB(dta_ds) != 0xA5 || s >= FIND_SLOTS || !finds[s].used) { *ax = 0x12; return false; }
    return find_fill(s, dta_ds, ax);
}

/* ------------------------------------------------------------------ RTL error helpers */

/* INTRO 0000:08a0 / PS 0000:1bea / AP 0000:1fa3 __IOerror */
s16 rtl_IOerror(s16 code)
{
    s16 si = code;
    if (si >= 0) {
        if (si > 0x58) si = 0x57;
        DSS(RL(doserrno)) = si;
        si = (s16)(s8)DSB((u16)(RL(dos_err_tab) + si));
    } else {
        si = (s16)-si;
        if (si > 0x30) {
            si = 0x57;
            DSS(RL(doserrno)) = si;
            si = (s16)(s8)DSB((u16)(RL(dos_err_tab) + si));
        } else {
            DSS(RL(doserrno)) = -1;
        }
    }
    DSS(RTL_DS_errno) = si;
    return -1;
}

/* INTRO 0000:08d9 / PS 0000:1c23 / AP 0000:1fdc __DOSerror */
s16 rtl_DOSerror(s16 code)
{
    rtl_IOerror(code);
    return code;
}

/* ------------------------------------------------------------------ low-level I/O */

static u16 openfd(s16 fd) { return (u16)(RL(openfd) + 2 * fd); }

/* INTRO 0000:1e4d / PS 0000:2b31 / AP 0000:33bd _chmod(path, func, attr) */
s16 rtl__chmod(u16 path_ds, s16 func, u16 attr)
{
    u16 cx, ax;
    if (func == 0) {
        if (!dos_getattr(path_ds, &cx, &ax)) return rtl_IOerror((s16)ax);
        return (s16)cx;
    }
    (void)attr;                                      /* PORT: setting attributes is a no-op */
    if (!dos_getattr(path_ds, &cx, &ax)) return rtl_IOerror((s16)ax);
    return (s16)attr;
}

/* INTRO 0000:2882 / PS 0000:35a8 / AP 0000:3e4e __creat(attr, path) */
static s16 rtl___creat(u16 attr, u16 path_ds)
{
    u16 ax;
    if (!dos_creat(path_ds, attr, &ax)) return rtl_IOerror((s16)ax);
    return (s16)ax;
}

/* INTRO 0000:289b / PS 0000:35c1 / AP 0000:3e67 __trunc(fd): INT 21h/40h with CX = 0 */
static void rtl___trunc(s16 fd)
{
    u16 ax;
    dos_write((u16)fd, far_make(cur_dgroup, 0), 0, &ax);
}

/* INTRO 0000:2a05 / PS 0000:372b / AP 0000:3fd1 _open(path, oflag) */
s16 rtl__open(u16 path_ds, u16 oflag)
{
    u8 al = 1;
    if (!(oflag & 2)) { al = 2; if (!(oflag & 4)) al = 0; }
    al |= (u8)(oflag & 0xF0);
    u16 ax;
    if (!dos_open(path_ds, al, &ax)) return rtl_IOerror((s16)ax);
    DSW(openfd((s16)ax)) = (u16)((oflag & 0xB8FF) | 0x8000);
    return (s16)ax;
}

/* INTRO 0000:1996 / PS 0000:2686 / AP 0000:2f53 ioctl(fd, func, dx, cx) (only func 0/1 used) */
s16 rtl_ioctl(s16 fd, s16 func, u16 dx, u16 cx)
{
    (void)cx;
    u16 rdx, ax;
    if (func == 0) {
        if (!dos_ioctl_info((u16)fd, &rdx, &ax)) return rtl_IOerror((s16)ax);
        return (s16)rdx;
    }
    if (func == 1) {                                 /* set device info (raw mode): accepted */
        if (!dos_ioctl_info((u16)fd, &rdx, &ax)) return rtl_IOerror((s16)ax);
        (void)dx;
        return (s16)ax;
    }
    host_fatal("ioctl function %d is not modelled", func);
}

/* INTRO 0000:08eb / PS 0000:1c35 / AP 0000:1fee isatty(fd): DX & 80h (no CF check) */
s16 rtl_isatty(s16 fd)
{
    u16 dx = 0, ax;
    if (!dos_ioctl_info((u16)fd, &dx, &ax)) dx = ax;   /* on error DX is not loaded: AX garbage */
    return (s16)(dx & 0x80);
}

/* INTRO 0000:28ad / PS 0000:35d3 / AP 0000:3e79 open(path, access, mode) */
s16 rtl_open(u16 path_ds, u16 access, ...)
{
    va_list ap;
    va_start(ap, access);
    u16 mode = (u16)va_arg(ap, unsigned);            /* read even without O_CREAT, as the original */
    va_end(ap);
    u16 si = access, di = mode;
    if (!(si & 0xC000)) si |= DSW(RL(fmode)) & 0xC000;
    s16 attr = rtl__chmod(path_ds, 0, 0);
    s16 fd;
    if (si & 0x100) {                                /* O_CREAT */
        di &= DSW(RL(notumask));
        if (!(di & 0x180)) rtl_IOerror(1);
        if (attr == -1) {
            if (DSW(RL(doserrno)) != 2) return rtl_IOerror(DSS(RL(doserrno)));
            attr = (di & 0x80) ? 0 : 1;
            if (si & 0xF0) {
                fd = rtl___creat(0, path_ds);
                if (fd < 0) return fd;
                rtl__close(fd);
                goto do_open;
            }
            fd = rtl___creat((u16)attr, path_ds);
            if (fd < 0) return fd;
            goto set_flags;
        }
        if (si & 0x400) return rtl_IOerror(0x50);    /* O_EXCL: file exists */
    }
do_open:
    fd = rtl__open(path_ds, si);
    if (fd >= 0) {
        u16 dev = (u16)rtl_ioctl(fd, 0, 0, 0);
        if (dev & 0x80) {
            si |= 0x2000;
            if (si & 0x8000) rtl_ioctl(fd, 1, (u16)((dev & 0xFF) | 0x20), 0);
        } else if (si & 0x200) {
            rtl___trunc(fd);
        }
        if ((attr & 1) && (si & 0x100) && (si & 0xF0)) rtl__chmod(path_ds, 1, 1);
    }
set_flags:
    if (fd >= 0) {
        u16 f = (u16)((si & 0xF8FF) | ((si & 0x300) ? 0x1000 : 0));
        f |= (attr & 1) ? 0 : 0x100;
        DSW(openfd(fd)) = f;
    }
    return fd;
}

/* INTRO 0000:1e90 / PS 0000:2b74 / AP 0000:3400 _close(fd) */
s16 rtl__close(s16 fd)
{
    u16 ax;
    if (!dos_close((u16)fd, &ax)) return rtl_IOerror((s16)ax);
    DSW(openfd(fd)) = 0;
    return 0;
}

/* INTRO 0000:1e68 / PS 0000:2b4c / AP 0000:33d8 close(fd) */
s16 rtl_close(s16 fd)
{
    if ((u16)fd >= DSW(RL(nfile))) return rtl_IOerror(6);
    DSW(openfd(fd)) = 0;
    return rtl__close(fd);
}

/* INTRO 0000:09b2 / PS 0000:1d24 / AP 0000:20dd lseek(fd, offset, whence) */
s32 rtl_lseek(s16 fd, s32 offset, s16 whence)
{
    DSW(openfd(fd)) &= 0xFDFF;
    u32 pos;
    u16 ax;
    if (!dos_lseek((u16)fd, offset, (u8)whence, &pos, &ax)) return (s32)rtl_IOerror((s16)ax);
    return (s32)pos;
}

/* INTRO 0000:12c3 / PS 0000:1f87 / AP 0000:231f tell(fd) */
s32 rtl_tell(s16 fd) { return rtl_lseek(fd, 0, 1); }

/* INTRO 0000:0add / PS 0000:1de6 / AP 0000:2213 _rtl_read(fd, buf, n) */
s16 rtl__read(s16 fd, u16 buf_ds, u16 n)
{
    if (DSW(openfd(fd)) & 2) return rtl_IOerror(5);
    u16 ax;
    if (!dos_read((u16)fd, ds_ptr(buf_ds), n, &ax)) return rtl_IOerror((s16)ax);
    return (s16)ax;
}

/* INTRO 0000:3805 / PS 0000:46c7 / AP 0000:4a36 _rtl_write(fd, buf, n) */
s16 rtl__write(s16 fd, u16 buf_ds, u16 n)
{
    if (DSW(openfd(fd)) & 1) return rtl_IOerror(5);
    u16 ax;
    if (!dos_write((u16)fd, ds_ptr(buf_ds), n, &ax)) return rtl_IOerror((s16)ax);
    DSW(openfd(fd)) |= 0x1000;
    return (s16)ax;
}

/* INTRO 0000:2d72 / PS 0000:3c8b / AP 0000:4531 read(fd, buf, n): text mode drops CR, stops at ^Z.
 * (INTRO 0000:2e40 / PS 0000:3d59 / AP 0000:45ff, listed as "remove", is a JMP to it.) */
s16 rtl_read(s16 fd, u16 buf_ds, u16 n)
{
    if ((u16)fd >= DSW(RL(nfile))) return rtl_IOerror(6);
    if ((u16)(n + 1) < 2 || (DSW(openfd(fd)) & 0x200)) return 0;
    for (;;) {
        s16 got = rtl__read(fd, buf_ds, n);
        if ((u16)(got + 1) < 2 || !(DSW(openfd(fd)) & 0x4000)) return got;
        u16 cx = (u16)got, si = buf_ds, di = buf_ds, bx = buf_ds;
        for (;;) {
            u8 al = DSB(si++);
            if (al == 0x1A) {                        /* ^Z: seek back over the rest, set EOF */
                rtl_lseek(fd, -(s32)cx, 1);
                DSW(openfd(fd)) |= 0x200;
                return (s16)(di - bx);
            }
            if (al == 0x0D) {
                if (--cx == 0) {                     /* CR was the last byte: read one more */
                    u16 m = stk_mark();
                    u16 t = stk_alloc(2);
                    rtl__read(fd, t, 1);
                    DSB(di++) = DSB(t);
                    stk_release(m);
                    break;
                }
                continue;
            }
            DSB(di++) = al;
            if (--cx == 0) break;
        }
        if (di != bx) return (s16)(di - bx);
    }
}

/* INTRO 0000:36f7 / PS 0000:45b9 / AP 0000:4928 write(fd, buf, n): text mode expands LF -> CR LF */
s16 rtl_write(s16 fd, u16 buf_ds, u16 n)
{
    if ((u16)fd >= DSW(RL(nfile))) return rtl_IOerror(6);
    if ((u16)(n + 1) < 2) return 0;
    if (DSW(openfd(fd)) & 0x800) rtl_lseek(fd, 0, 2);
    if (!(DSW(openfd(fd)) & 0x4000)) return rtl__write(fd, buf_ds, n);
    DSW(openfd(fd)) &= 0xFDFF;
    u16 m = stk_mark();
    u16 tmp = stk_alloc(0x80 + 2);                   /* [bp-88h]: 128-byte translation buffer */
    u16 src = buf_ds, left = n, si = tmp;
    s16 r;
    while (left) {
        left--;
        u8 c = DSB(src++);
        if (c == 0x0A) DSB(si++) = 0x0D;
        DSB(si++) = c;
        u16 len = (u16)(si - tmp);
        if ((s16)len < 0x80) continue;
        s16 w = rtl__write(fd, tmp, len);
        if ((u16)w != len) {
            if (w == -1) { r = -1; goto out; }
            r = (s16)(n - left + w - len);
            goto out;
        }
        si = tmp;
    }
    {
        u16 len = (u16)(si - tmp);
        if (len) {
            s16 w = rtl__write(fd, tmp, len);
            if ((u16)w != len) {
                if (w == -1) { r = -1; goto out; }
                r = (s16)(n + w - len);
                goto out;
            }
        }
        r = (s16)n;
    }
out:
    stk_release(m);
    return r;
}

/* INTRO 0000:1eae / PS 0000:2b92 / AP 0000:341e eof(fd) */
s16 rtl_eof(s16 fd)
{
    if ((u16)fd >= DSW(RL(nfile))) return rtl_IOerror(6);
    if (DSW(openfd(fd)) & 0x200) return 1;
    u16 dx, ax;
    if (!dos_ioctl_info((u16)fd, &dx, &ax)) return rtl_IOerror((s16)ax);
    if (dx & 0x80) return 0;
    u32 cur, end, p;
    if (!dos_lseek((u16)fd, 0, 1, &cur, &ax)) return rtl_IOerror((s16)ax);
    if (!dos_lseek((u16)fd, 0, 2, &end, &ax)) return rtl_IOerror((s16)ax);
    if (!dos_lseek((u16)fd, (s32)cur, 0, &p, &ax)) return rtl_IOerror((s16)ax);
    return cur >= end ? 1 : 0;
}

/* ------------------------------------------------------------------ the rest of rtl_dos.c */

/* INTRO 0000:04c7 / PS 0000:1811 / AP 0000:1b37 chdir */
s16 rtl_chdir(u16 path_ds)
{
    u16 ax;
    if (!dos_chdir(path_ds, &ax)) return rtl_IOerror((s16)ax);
    return 0;
}

/* INTRO 0000:1309 / AP 0000:2240 unlink (also PS 0000:1ff9 / AP 0000:2365, the copy fclose uses) */
s16 rtl_unlink(u16 path_ds)
{
    u16 ax;
    if (!dos_unlink(path_ds, &ax)) return rtl_IOerror((s16)ax);
    return 0;
}

/* AP 0000:4602 rename */
s16 rtl_rename(u16 old_ds, u16 new_ds)
{
    u16 ax;
    if (!dos_rename(old_ds, new_ds, &ax)) return rtl_IOerror((s16)ax);
    return 0;
}

/* AP 0000:1cbc getdisk: INT 21h/19h + 1, then - 1 = 2 (C:) */
s16 rtl_getdisk(void) { return 2; }

/* AP 0000:1d1f getdfree(drive, struct dfree *): INT 21h/36h. PORT: a large free C: drive
 * (dfree: avail clusters, total, bytes/sector, sectors/cluster at +0/+2/+4/+6). */
void rtl_getdfree(u8 drive, u16 dfree_ds)
{
    (void)drive;
    DSW(dfree_ds + 0) = 0x7FFF;                      /* BX: available clusters */
    DSW(dfree_ds + 2) = 0xFFFE;                      /* DX: total clusters */
    DSW(dfree_ds + 4) = 512;                         /* CX: bytes per sector */
    DSW(dfree_ds + 6) = 64;                          /* AX: sectors per cluster (32 KB clusters) */
}

/* INTRO 0000:19bd / PS 0000:26ad / AP 0000:2f7a kbhit: the pending ungetch flag, else INT 21h/0Bh
 * (the BIOS buffer; host_pump runs inside bios_key_available). */
s16 rtl_kbhit(void)
{
    if (DSB(RL(cvt_ch))) return 1;
    return bios_key_available() ? -1 : 0;           /* AL = FFh -> cbw = -1 */
}

/* INTRO 0000:0a74 outport(port, value). PORT: only the VGA ports are modelled. */
void rtl_outport(u16 port, u16 value)
{
    if (port >= 0x3C0 && port <= 0x3DA) vga_outw(port, value);
}

/* INTRO 0000:18f2 / PS 0000:25e2 / AP 0000:2eaf int86(intno, inregs, outregs). The only callers are
 * the Windows helpers (INT 2Fh, dropped: win_detect is 0) - PORT: INT 2Fh returns AX unchanged except
 * 1600h -> AL = 0 (no Windows); anything else is fatal. REGS = {ax, bx, cx, dx, si, di, cflag, flags}. */
s16 rtl___int86(s16 intno, u16 inregs_ds, u16 outregs_ds)
{
    if (intno != 0x2F) host_fatal("int86(%02Xh) is not modelled", intno);
    u16 ax = DSW(inregs_ds);
    for (int i = 0; i < 8; i++) DSW(outregs_ds + 2 * i) = DSW(inregs_ds + 2 * i);
    if (ax == 0x1600) ax = 0x1600 & 0xFF00;
    DSW(outregs_ds) = ax;
    DSW(outregs_ds + 12) = 0;                        /* cflag */
    return (s16)ax;
}
