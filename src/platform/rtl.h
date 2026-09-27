#pragma once
/* Borland C++ 3.0 RTL functions the game code calls (medium model), implemented once for the
 * three programs in platform/rtl_*.c (port/modules.csv kind "rtl"; internals "rtl_int" only as
 * far as these need them). Names: rtl_ + the Borland name.
 *
 * Conventions: a near pointer (char *, FILE *, void *) is a u16 DGROUP offset (*_ds); far
 * pointers are FarPtr. Varargs ("...") are the words of the original stack image passed as C
 * `unsigned` arguments (a long or a far pointer = two words, low word first), so __vprinter
 * reads them exactly like the original; the v* forms take the DS offset of such a word list
 * (va_ds, e.g. built on the emulated stack with stk_alloc). exit/atexit are prog_exit/
 * prog_atexit (prog.h), getvect/setvect ivt_get/ivt_set (mem.h), the long-arithmetic and
 * huge-pointer helpers (LDIV@, LXMUL@, PADD@ ... kind "rtl_op") are C operators and far_*().
 * Globals (_streams, errno, _openfd, the heap) stay in mem[] at each program's DS offsets (G_ ids).
 * Addresses: INTRO / PS / AP where the program has the function. */
#include "mem.h"

/* A qsort comparator (a far code pointer in the original) is registered with codeptr and called
 * with the DS offsets of the two elements; it returns the int the original leaves in AX. */
typedef s16 (*RtlCmpFn)(u16 a_ds, u16 b_ds);

/* ==== platform/rtl_dos.c ========================================================================== */
/* INTRO 0000:18f2 / PS 0000:25e2 / AP 0000:2eaf __int86 */
s16 rtl___int86(s16 intno, u16 inregs_ds, u16 outregs_ds);
/* INTRO 0000:04c7 / PS 0000:1811 / AP 0000:1b37 chdir */
s16 rtl_chdir(u16 path_ds);
/* INTRO 0000:1e68 / PS 0000:2b4c / AP 0000:33d8 close */
s16 rtl_close(s16 fd);
/* AP 0000:1d1f getdfree */
void rtl_getdfree(u8 drive, u16 dfree_ds);
/* AP 0000:1cbc getdisk */
s16 rtl_getdisk(void);
/* INTRO 0000:19bd / PS 0000:26ad / AP 0000:2f7a kbhit */
s16 rtl_kbhit(void);
/* INTRO 0000:09b2 / PS 0000:1d24 / AP 0000:20dd lseek */
s32 rtl_lseek(s16 fd, s32 offset, s16 whence);
/* INTRO 0000:28ad / PS 0000:35d3 / AP 0000:3e79 open */
s16 rtl_open(u16 path_ds, u16 access, ...);
/* INTRO 0000:0a74 outport */
void rtl_outport(u16 port, u16 value);
/* INTRO 0000:2d72 / PS 0000:3c8b / AP 0000:4531 read (text mode). HEADER FIX: INTRO 0000:2e40 /
 * PS 0000:3d59 / AP 0000:45ff (listed as "remove") are a JMP to read; only the DS:1A04 callback
 * wrappers call them. */
s16 rtl_read(s16 fd, u16 buf_ds, u16 n);
/* AP 0000:4602 rename */
s16 rtl_rename(u16 old_ds, u16 new_ds);
/* INTRO 0000:12c3 / PS 0000:1f87 / AP 0000:231f tell */
s32 rtl_tell(s16 fd);
/* INTRO 0000:1309 / AP 0000:2240 unlink */
s16 rtl_unlink(u16 path_ds);

/* ==== platform/rtl_heap.c ========================================================================= */
/* INTRO 0000:1882 / PS 0000:2572 / AP 0000:28de calloc */
u16 rtl_calloc(u16 n, u16 size);
/* INTRO 0000:19cf / PS 0000:26bf / AP 0000:2f8c free */
void rtl_free(u16 block_ds);
/* INTRO 0000:1a9e / PS 0000:278e / AP 0000:305b malloc — Borland near heap between BSS and the stack, block addresses as the original (PORTING.md memory model); 0 = out of memory */
u16 rtl_malloc(u16 size);

/* ==== platform/rtl_printf.c ======================================================================= */
/* INTRO 0000:1dd0 / PS 0000:2ab4 / AP 0000:3340 atol */
s32 rtl_atol(u16 s_ds);
/* INTRO 0000:22c3 / PS 0000:2f45 / AP 0000:37d1 fprintf */
s16 rtl_fprintf(u16 fp_ds, u16 fmt_ds, ...);
/* INTRO 0000:27bb / PS 0000:343d / AP 0000:3d02 itoa */
u16 rtl_itoa(s16 value, u16 buf_ds, s16 radix);
/* INTRO 0000:27fd / PS 0000:347f / AP 0000:3d44 ltoa */
u16 rtl_ltoa(s32 value, u16 buf_ds, s16 radix);
/* INTRO 0000:2a52 / PS 0000:3778 / AP 0000:401e printf */
s16 rtl_printf(u16 fmt_ds, ...);
/* PS 0000:3e5c / AP 0000:4741 sprintf — the Borland __vprinter formatting (%p, %Fs, %ld ... as the original) */
s16 rtl_sprintf(u16 buf_ds, u16 fmt_ds, ...);
/* INTRO 0000:2f68 sscanf */
s16 rtl_sscanf(u16 s_ds, u16 fmt_ds, ...);
/* INTRO 0000:36e1 / PS 0000:45a3 vprintf */
s16 rtl_vprintf(u16 fmt_ds, u16 va_ds);

/* ==== platform/rtl_stdio.c ======================================================================== */
/* PS 0000:474c / AP 0000:4abb _fmemcpy */
FarPtr rtl__fmemcpy(FarPtr dst, FarPtr src, u16 n);
/* PS 0000:4794 / AP 0000:4b03 _fmemset */
FarPtr rtl__fmemset(FarPtr dst, s16 c, u16 n);
/* PS 0000:47b3 / AP 0000:4b22 _fstrcat */
FarPtr rtl__fstrcat(FarPtr dst, FarPtr src);
/* PS 0000:47f2 _fstrchr */
FarPtr rtl__fstrchr(FarPtr s, s16 c);
/* AP 0000:4b61 _fstrcmp */
s16 rtl__fstrcmp(FarPtr a, FarPtr b);
/* INTRO 0000:388a / PS 0000:482f / AP 0000:4b91 _fstrcpy */
FarPtr rtl__fstrcpy(FarPtr dst, FarPtr src);
/* PS 0000:4858 / AP 0000:4bba _fstricmp */
s16 rtl__fstricmp(FarPtr a, FarPtr b);
/* PS 0000:4899 / AP 0000:4bfb _fstrlen */
u16 rtl__fstrlen(FarPtr s);
/* INTRO 0000:38b3 / PS 0000:48b8 / AP 0000:4c1a _fstrncpy */
FarPtr rtl__fstrncpy(FarPtr dst, FarPtr src, u16 n);
/* INTRO 0000:1f25 / PS 0000:2c09 / AP 0000:3495 fclose */
s16 rtl_fclose(u16 fp_ds);
/* INTRO 0000:1fa6 / PS 0000:2c8a / AP 0000:3516 fflush */
s16 rtl_fflush(u16 fp_ds);
/* INTRO 0000:26a3 / PS 0000:3325 / AP 0000:3bb1 fgetc */
s16 rtl_fgetc(u16 fp_ds);
/* INTRO 0000:2027 fgets */
u16 rtl_fgets(u16 buf_ds, s16 n, u16 fp_ds);
/* INTRO 0000:2089 / PS 0000:2d0b / AP 0000:3597 findfirst */
s16 rtl_findfirst(u16 path_ds, u16 ffblk_ds, s16 attrib);
/* INTRO 0000:20bc / PS 0000:2d3e / AP 0000:35ca findnext */
s16 rtl_findnext(u16 ffblk_ds);
/* INTRO 0000:22a4 / PS 0000:2f26 / AP 0000:37b2 fopen — returns the DS offset of a Borland FILE record in DGROUP (_streams), 0 = failure; paths go through the DOS work directory mapping (host_game_path) */
u16 rtl_fopen(u16 path_ds, u16 mode_ds);
/* INTRO 0000:2a82 / PS 0000:37a8 / AP 0000:404e fputc */
s16 rtl_fputc(s16 c, u16 fp_ds);
/* INTRO 0000:22d9 / PS 0000:2f5b / AP 0000:37e7 fputs */
s16 rtl_fputs(u16 s_ds, u16 fp_ds);
/* INTRO 0000:23e9 / PS 0000:306b / AP 0000:38f7 fread */
u16 rtl_fread(u16 buf_ds, u16 size, u16 n, u16 fp_ds);
/* INTRO 0000:2491 / PS 0000:3113 / AP 0000:399f fseek */
s16 rtl_fseek(u16 fp_ds, s32 offset, s16 whence);
/* INTRO 0000:24f9 / PS 0000:317b / AP 0000:3a07 ftell */
s32 rtl_ftell(u16 fp_ds);
/* INTRO 0000:25c0 / PS 0000:3242 / AP 0000:3ace fwrite */
u16 rtl_fwrite(u16 buf_ds, u16 size, u16 n, u16 fp_ds);
/* INTRO 0000:2e43 / AP 0000:461f rewind */
void rtl_rewind(u16 fp_ds);
/* INTRO 0000:114f / PS 0000:1e13 / AP 0000:2256 setbuf */
void rtl_setbuf(u16 fp_ds, u16 buf_ds);

/* ==== platform/rtl_stdlib.c ======================================================================= */
/* INTRO 0000:0996 / PS 0000:1d08 / AP 0000:20c1 _lrotl */
u32 rtl__lrotl(u32 value, s16 count);
/* INTRO 0000:060f / PS 0000:1959 / AP 0000:1ca9 gettime */
void rtl_gettime(u16 time_ds);
/* PS 0000:1c46 / AP 0000:1fff labs */
s32 rtl_labs(s32 x);
/* PS 0000:3c66 / AP 0000:450c qsort — Borland's own algorithm, bit-exact (shell_data_emd1.md §5 relies on its tie order); cmp is a far code pointer called as int far cmp(void *a, void *b) through codeptr */
void rtl_qsort(u16 base_ds, u16 n, u16 width, FarPtr cmp);
/* INTRO 0000:0ab7 / AP 0000:21ed rand — Borland LCG: seed = seed * 0x015A4E35 + 1, returns (seed >> 16) & 0x7FFF */
s16 rtl_rand(void);
/* INTRO 0000:0aa6 / AP 0000:21dc srand */
void rtl_srand(u16 seed);
/* INTRO 0000:1276 / PS 0000:1f3a time — host clock; DOS local time as Borland computes it (tzset/TZ default EST5EDT) */
s32 rtl_time(u16 tloc_ds);

/* ==== platform/rtl_string.c ======================================================================= */
/* PS 0000:34a4 memchr */
u16 rtl_memchr(u16 s_ds, s16 c, u16 n);
/* INTRO 0000:2822 / PS 0000:34c3 / AP 0000:3d69 memcpy */
u16 rtl_memcpy(u16 dst_ds, u16 src_ds, u16 n);
/* PS 0000:358e / AP 0000:3e34 memmove */
u16 rtl_memmove(u16 dst_ds, u16 src_ds, u16 n);
/* INTRO 0000:2867 / PS 0000:3508 / AP 0000:3dae memset */
u16 rtl_memset(u16 dst_ds, s16 c, u16 n);
/* PS 0000:3523 / AP 0000:3dc9 movedata */
void rtl_movedata(u16 srcseg, u16 srcoff, u16 dstseg, u16 dstoff, u16 n);
/* INTRO 0000:2fa3 / PS 0000:3e95 / AP 0000:477a strcat */
u16 rtl_strcat(u16 dst_ds, u16 src_ds);
/* INTRO 0000:2fdc / PS 0000:3ece / AP 0000:47b3 strchr */
u16 rtl_strchr(u16 s_ds, s16 c);
/* INTRO 0000:3012 / PS 0000:3f04 strcmp */
s16 rtl_strcmp(u16 a_ds, u16 b_ds);
/* INTRO 0000:3041 / PS 0000:3f33 / AP 0000:47e9 strcpy */
u16 rtl_strcpy(u16 dst_ds, u16 src_ds);
/* INTRO 0000:3063 / PS 0000:3f55 / AP 0000:480b stricmp */
s16 rtl_stricmp(u16 a_ds, u16 b_ds);
/* INTRO 0000:30a3 / PS 0000:3f95 / AP 0000:484b strlen */
u16 rtl_strlen(u16 s_ds);
/* INTRO 0000:30bd / PS 0000:3faf strncpy */
u16 rtl_strncpy(u16 dst_ds, u16 src_ds, u16 n);
/* AP 0000:4865 strnicmp */
s16 rtl_strnicmp(u16 a_ds, u16 b_ds, u16 n);
/* INTRO 0000:30e9 / PS 0000:3fdb strrchr */
u16 rtl_strrchr(u16 s_ds, s16 c);
/* PS 0000:400a strrev */
u16 rtl_strrev(u16 s_ds);
/* INTRO 0000:3118 / AP 0000:48a8 strstr */
u16 rtl_strstr(u16 s_ds, u16 sub_ds);
/* INTRO 0000:3178 / PS 0000:403a / AP 0000:4908 strupr */
u16 rtl_strupr(u16 s_ds);
/* INTRO 0000:12dd / PS 0000:1fa1 tolower */
s16 rtl_tolower(s16 c);
/* PS 0000:1fcd / AP 0000:2339 toupper (HEADER FIX: AP 0000:2339 was listed as tolower; its code tests
 * _IS_LOW and subtracts 20h) */
s16 rtl_toupper(s16 c);
