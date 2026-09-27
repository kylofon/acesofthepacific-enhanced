#pragma once
/* The three programs in one process (platform.md §2.1–2.3, §9.1; PLAN.md decision 2).
 *
 * main() runs GCD.COM's chain loop: prog_run(code) loads INTRO.EXE (1), PS.EXE (2) or AP.EXE (3) from
 * the game folder, re-initialises DOS + Borland c0 state (PSP, environment, DGROUP: data from the image,
 * BSS zeroed, stack at the top of the 64 KB DGROUP, argv) and calls the program's main. exit() inside a
 * program is prog_exit(): the atexit chain runs, then control longjmps back to prog_run, which returns
 * the exit code (0 = quit to DOS, 1..3 = next program). */
#include "mem.h"

enum { PROG_INTRO = 1, PROG_PS = 2, PROG_AP = 3 };

/* The virtual DOS work directory. The port passes GCD's child tail " -n<dir>\" to every program, like
 * AOTPCD.BAT (gcd -nC:\DYNAMIX\AOTP); the file layer maps this directory to the game folder
 * (host_game_path). Keeping the DOS spelling keeps the programs' 80-byte path buffers safe. */
#define DOS_WORK_DIR "C:\\DYNAMIX\\AOTP"

typedef int (*ProgMain)(int argc, u16 argv_ds);        /* argv = DS offset of the char *argv[] array */

typedef struct {
    int id;
    const char *exe;              /* file name in the game folder */
    const char *mcb_name;         /* program name in its MCB */
    bool flatten;                 /* AP: flatten the VROOMM overlays */
    u32 image_len;                /* expected load image length (after flattening) */
    u16 dgroup_file;              /* DGROUP file segment */
    u16 bss_start, bss_end;       /* c0 zeroes DS:bss_start .. bss_end-1; bss_end = static DGROUP size */
    u16 stklen_ds, heaplen_ds;    /* _stklen / _heaplen */
    u16 id_ofs;                   /* identity check: this string at DS:id_ofs */
    const char *id_str;
    const ProgLayout *layout;
    ProgMain main;
} ProgDesc;

const ProgDesc *prog_desc(int id);
int  prog_current_id(void);                   /* 0 while no program runs */
const ProgDesc *prog_current(void);

/* Loads and runs one program; returns its exit code (AL). extra_args are placed in front of the
 * " -n<dir>\" tail (so AP's debug word "nlkle" can be argv[1]). */
int  prog_run(int id, const char *extra_args);

/* Borland exit(code): atexit chain (reverse order), then back to the chain loop. */
_Noreturn void prog_exit(int code);
/* Borland atexit(): at most 32 functions, returns 0 on success. PORT: the table is host-side (no code
 * reads Borland's _atexittbl). */
int  prog_atexit(void (*fn)(void));

/* Loads and checks all three executables without running them (--check). Returns false on error. */
bool prog_check_all(const char *flat_reference, char *err, size_t errlen);
