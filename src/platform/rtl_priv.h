#pragma once
/* Borland C++ 3.0 RTL internals shared by platform/rtl_*.c (not for game code).
 *
 * The RTL globals (_streams, _openfd, errno, the near-heap pointers ...) were found by aligning the RTL
 * functions of INTRO, PS and AP (byte-identical apart from DS offsets); they are symbols.csv rows
 * (port/spec/platform_extra2_symbols.csv) and reached through the layout table: RL(field) = the DS
 * offset of the current program's global (fatal where the program has none). The globals themselves
 * stay in mem[]. */
#include <stdarg.h>

#include "platform/platform.h"

/* RTL field -> layout id */
#define RLG_doserrno       G__doserrno
#define RLG_dos_err_tab    G__dosErrorToSV
#define RLG_streams        G__streams
#define RLG_nfile          G__nfile
#define RLG_openfd         G__openfd
#define RLG_fmode          G__fmode
#define RLG_notumask       G__notUmask
#define RLG_exitbuf        G__exitbuf
#define RLG_exitfopen      G__exitfopen
#define RLG_exitopen       G__exitopen
#define RLG_stdin_setbuf   G__stdinbuf_set
#define RLG_stdout_setbuf  G__stdoutbuf_set
#define RLG_first          G___first
#define RLG_last           G___last
#define RLG_rover          G___rover
#define RLG_cvt_ch         G__cFlag
#define RLG_ctype1         G__ctype_1
#define RLG_rand_seed      G__rand_seed
#define RLG_cr_str         G__fputc_cr
#define RLG_fputc_ch       G__fputc_ch
#define RLG_fgetc_ch       G__fgetc_ch
#define RLG_qs_width       G__qs_width
#define RLG_qs_cmp         G__qs_cmp
#define RLG_null_str       G__vpr_null
#define RLG_pf_class       G__vpr_class
#define RLG_environ        G_environ
#define RLG_tzname         G_tzname
#define RLG_timezone       G_timezone
#define RLG_daylight       G_daylight
#define RLG_tz_str         G__tz_TZ
#define RLG_est_str        G__tz_EST
#define RLG_edt_str        G__tz_EDT
#define RLG_month_days     G__Days
#define RLG_cum_days       G__YDays
#define RL(field) lds(RLG_##field)
/* code offsets (segment 0000) of the exit-hook targets _xfflush and _xfclose */
#define RTL_XFFLUSH_OFF ((u16)prog_layout->fn[F_rtl__xfflush])
#define RTL_XFCLOSE_OFF ((u16)prog_layout->fn[F_rtl__xfclose])

/* Fixed c0 globals (the same DS offsets in the three programs). */
#define RTL_DS_errno      0x0094
#define RTL_DS_heapbase   0x009A
#define RTL_DS_brklvl     0x009C
#define RTL_DS_psp        0x0090
#define RTL_DS_version    0x0092
#define RTL_DS_envLng     0x008A
#define RTL_DS_envseg     0x008C
#define RTL_DS_envSize    0x008E
#define RTL_DS_environ88  0x0088

/* Borland FILE (medium model, near data, 10h bytes). */
#define FILE_level   0x00   /* s16 */
#define FILE_flags   0x02   /* u16 */
#define FILE_fd      0x04   /* s8  */
#define FILE_hold    0x05   /* u8  */
#define FILE_bsize   0x06   /* s16 */
#define FILE_buffer  0x08   /* u16 near */
#define FILE_curp    0x0A   /* u16 near */
#define FILE_istemp  0x0C   /* u16 */
#define FILE_token   0x0E   /* u16 (= the FILE's own offset when valid) */
#define FILE_SIZE    0x10

#define F_RDWR  0x0003
#define F_READ  0x0001
#define F_WRIT  0x0002
#define F_BUF   0x0004
#define F_LBUF  0x0008
#define F_ERR   0x0010
#define F_EOF   0x0020
#define F_BIN   0x0040
#define F_IN    0x0080
#define F_OUT   0x0100
#define F_TERM  0x0200

/* _openfd[] flags */
#define O_RDONLY_ 0x0001
#define O_WRONLY_ 0x0002
#define O_RDWR_   0x0004
#define O_CREAT_  0x0100
#define O_TRUNC_  0x0200
#define O_EXCL_   0x0400
#define O_APPEND_ 0x0800
#define O_CHANGED_ 0x1000
#define O_DEVICE_ 0x2000
#define O_TEXT_   0x4000
#define O_BINARY_ 0x8000
#define O_EOF_    0x0200  /* in _openfd: Ctrl-Z seen */

/* ---- rtl_dos.c: the DOS file layer (INT 21h functions on host files) and the RTL error helpers. */
s16  rtl_IOerror(s16 code);                 /* __IOerror: errno/_doserrno, returns -1 */
s16  rtl_DOSerror(s16 code);                /* __DOSerror: returns code */
/* INT 21h model. Each returns true on success (CF clear) with *ax the result, false with *ax = the
 * DOS error code. Paths are DS offsets of DOS path strings (C:\DYNAMIX\AOTP\... or relative). */
bool dos_open(u16 path_ds, u8 mode, u16 *ax);          /* 3Dh */
bool dos_creat(u16 path_ds, u16 attr, u16 *ax);        /* 3Ch */
bool dos_close(u16 h, u16 *ax);                        /* 3Eh */
bool dos_read(u16 h, FarPtr buf, u16 n, u16 *ax);      /* 3Fh */
bool dos_write(u16 h, FarPtr buf, u16 n, u16 *ax);     /* 40h (n = 0 truncates) */
bool dos_lseek(u16 h, s32 off, u8 whence, u32 *pos, u16 *ax);   /* 42h */
bool dos_getattr(u16 path_ds, u16 *cx, u16 *ax);       /* 4300h */
bool dos_unlink(u16 path_ds, u16 *ax);                 /* 41h */
bool dos_rename(u16 old_ds, u16 new_ds, u16 *ax);      /* 56h */
bool dos_ioctl_info(u16 h, u16 *dx, u16 *ax);          /* 4400h */
bool dos_chdir(u16 path_ds, u16 *ax);                  /* 3Bh */
bool dos_findfirst(u16 path_ds, u16 attr, u16 dta_ds, u16 *ax);   /* 1Ah + 4Eh */
bool dos_findnext(u16 dta_ds, u16 *ax);                /* 1Ah + 4Fh */
/* Host path of a DOS name (malloc'd, host_free) or NULL. create = the file may not exist yet. */
char *dos_host_path(const char *dos_name, bool create);
void dos_close_all(void);                              /* process exit: DOS closes the handles */
void dos_prog_start(void);                             /* new program: handles 0..4 = devices */

s16  rtl__read(s16 fd, u16 buf_ds, u16 n);             /* _rtl_read */
s16  rtl__write(s16 fd, u16 buf_ds, u16 n);            /* _rtl_write */
s16  rtl_write(s16 fd, u16 buf_ds, u16 n);             /* write (text translation) */
s16  rtl_eof(s16 fd);
s16  rtl_isatty(s16 fd);
s16  rtl__close(s16 fd);
s16  rtl__open(u16 path_ds, u16 oflag);
s16  rtl__chmod(u16 path_ds, s16 func, u16 attr);
s16  rtl_ioctl(s16 fd, s16 func, u16 dx, u16 cx);

/* ---- rtl_heap.c */
s16  rtl___brk(u16 addr);
u16  rtl___sbrk(u32 incr);
u16  rtl_realloc(u16 block_ds, u16 size);
u16  rtl_setmem_ds(u16 dst_ds, u16 n, u8 c);           /* setmem */

/* ---- rtl_stdio.c */
s16  rtl_setvbuf(u16 fp_ds, u16 buf_ds, s16 type, u16 size);
u16  rtl___fputn(u16 fp_ds, u16 n, u16 s_ds);          /* returns n, or 0 on error */
s16  rtl__fgetc(u16 fp_ds);
s16  rtl__fputc(s16 c, u16 fp_ds);
void rtl_setupio(void);                                /* #pragma startup _setupio */
void rtl_exit_streams(void);                           /* exit(): _exitbuf, _exitfopen, _exitopen */
u16  rtl_stdout_fp(void);                              /* &_streams[1] */

/* ---- rtl_printf.c */
typedef u16 (*RtlPutFn)(u16 n, u16 s_ds, u16 arg);     /* __vprinter output: returns 0 on error */
s16  rtl___vprinter(RtlPutFn put, u16 put_arg, u16 fmt_ds, u16 va_ds);
/* Copies the variadic words for fmt onto the emulated stack; returns their DS offset. */
u16  rtl_va_words(u16 fmt_ds, const u16 *words, int nwords);
int  rtl_va_count(u16 fmt_ds);                         /* words a format consumes */
/* The words for fmt from a C va_list of unsigned, copied onto the emulated stack (DS offset). */
u16  rtl_va_from_list(u16 fmt_ds, va_list ap);
s16  rtl_vsprintf(u16 buf_ds, u16 fmt_ds, u16 va_ds);

/* ---- rtl_stdlib.c */

void rtl_tzset(void);
u16  rtl_getenv(u16 name_ds);

/* Small helpers: strings in the current DGROUP. */
static inline const char *ds_str(u16 o) { return (const char *)DSP(o); }
/* volume.c: a DS copy of a C string constant on the emulated stack (the caller releases the mark) */
u16 ds_lit(const char *s);
static inline u16 ds_strlen(u16 o) { u16 n = 0; while (DSB((u16)(o + n))) n++; return n; }
