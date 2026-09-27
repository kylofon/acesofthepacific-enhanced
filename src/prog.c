/* The three programs in one process: loading, DOS + Borland c0 start-up state, exit(). See prog.h. */
#include "prog.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "codeptr.h"
#include "game/game.h"
#include "host.h"
#include "platform/platform.h"

u16 cur_dgroup;
const ProgLayout *prog_layout;

static const ProgDesc progs[3] = {
    { PROG_INTRO, "INTRO.EXE", "INTRO", false, 0x1B310, 0x17E0, 0x242C, 0x348C, 0x2316, 0x222A,
      0x00AA, "vmcode.ovl", &layout_intro, intro_main },
    { PROG_PS, "PS.EXE", "PS", false, 0x54930, 0x4D08, 0x40CA, 0x782E, 0x3FB4, 0x3F4C,
      0x2036, "Aces Of The Pacific ver. 1.2", &layout_ps, ps_main },
    { PROG_AP, "AP.EXE", "AP", true, 0x728F0, 0x3EA4, 0x3AF4, 0x71F8, 0x05B2, 0x39AA,
      0x0541, "At least 112K EMS memory required", &layout_ap, ap_main },
};

static const ProgDesc *cur;
static jmp_buf exit_jmp;
static int exit_code;
static void (*atexit_fns[32])(void);
static int atexit_count;

const ProgDesc *prog_desc(int id)
{
    if (id < PROG_INTRO || id > PROG_AP) host_fatal("no program %d", id);
    return &progs[id - 1];
}

int prog_current_id(void) { return cur ? cur->id : 0; }
const ProgDesc *prog_current(void) { return cur; }

/* Load image checks shared by prog_run and --check. */
static bool check_image(const ProgDesc *d, const ExeImage *x, char *err, size_t errlen)
{
    if (x->image_len != d->image_len) {
        snprintf(err, errlen, "%s: load image is %05X bytes, expected %05X (CD version 1.2)", d->exe,
                 (unsigned)x->image_len, (unsigned)d->image_len);
        return false;
    }
    u32 at = (u32)d->dgroup_file * 16 + d->id_ofs;
    size_t n = strlen(d->id_str);
    if (at + n > x->image_len || memcmp(x->image + at, d->id_str, n) != 0) {
        snprintf(err, errlen, "%s is not the expected build (\"%s\" not at DS:%04X)", d->exe, d->id_str, d->id_ofs);
        return false;
    }
    return true;
}

static bool read_program(const ProgDesc *d, ExeImage *x, char *err, size_t errlen)
{
    char *path = host_game_path(d->exe, false);
    if (!path) { snprintf(err, errlen, "%s not found in the game folder %s", d->exe, host_game_dir()); return false; }
    bool ok = mem_read_exe(path, d->flatten, x, err, errlen);
    host_free(path);
    if (ok && !check_image(d, x, err, errlen)) { mem_free_exe(x); ok = false; }
    return ok;
}

/* Environment block and PSP as DOS builds them for EXEC. */
static u16 build_env_and_psp(const ProgDesc *d, const char *tail, u16 *env_bytes, u16 *env_strings)
{
    static const char *const env[] = { "COMSPEC=C:\\COMMAND.COM", "PATH=C:\\DOS", NULL };
    u16 o = 0;
    u16 n = 0;
    for (; env[n]; n++) {
        size_t l = strlen(env[n]) + 1;
        memcpy(mp(ENV_SEG, o), env[n], l);
        o = (u16)(o + l);
    }
    wr8(ENV_SEG, o++, 0);                                  /* end of the strings */
    *env_bytes = o;                                        /* c0: size incl. the final 0 */
    *env_strings = n;
    wr16(ENV_SEG, o, 1);                                   /* DOS 3+: count, then the program path */
    o = (u16)(o + 2);
    char path[80];
    snprintf(path, sizeof path, "%s\\%s", DOS_WORK_DIR, d->exe);
    memcpy(mp(ENV_SEG, o), path, strlen(path) + 1);

    wr16(PSP_SEG, 0x00, 0x20CD);                           /* INT 20h */
    wr16(PSP_SEG, 0x02, MEM_TOP_SEG);                      /* top of memory */
    wr16(PSP_SEG, 0x2C, ENV_SEG);
    size_t tl = strlen(tail);
    if (tl > 126) tl = 126;
    wr8(PSP_SEG, 0x80, (u8)tl);
    memcpy(mp(PSP_SEG, 0x81), tail, tl);
    wr8(PSP_SEG, (u16)(0x81 + tl), 0x0D);
    return o;                                              /* offset of argv[0] in the environment */
}

/* Borland _setargv: argv[0] = the program path, the tail split at blanks; strings and the array on the
 * stack at the top of DGROUP. Returns argc; *argv_ds = DS offset of argv[]. PORT: quote handling of
 * the RTL is not needed (the port's tails have none). */
static int build_argv(u16 path_ofs, const char *tail, u16 *argv_ds)
{
    const char *args[16];
    size_t lens[16];
    int argc = 0;
    args[argc] = (const char *)mp(ENV_SEG, path_ofs);
    lens[argc] = strlen(args[argc]);
    argc++;
    for (const char *p = tail; *p && argc < 16;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        args[argc] = s;
        lens[argc] = (size_t)(p - s);
        argc++;
    }
    u16 ptr[16];
    for (int i = argc - 1; i >= 0; i--) {
        u16 at = stk_alloc((u16)(lens[i] + 1));
        memcpy(DSP(at), args[i], lens[i]);
        DSB(at + lens[i]) = 0;
        ptr[i] = at;
    }
    u16 arr = stk_alloc((u16)((argc + 1) * 2));
    for (int i = 0; i < argc; i++) DSW(arr + 2 * i) = ptr[i];
    DSW(arr + 2 * argc) = 0;
    *argv_ds = arr;
    return argc;
}

int prog_run(int id, const char *extra_args)
{
    const ProgDesc *d = prog_desc(id);
    ExeImage x;
    char err[256];
    if (!read_program(d, &x, err, sizeof err)) host_fatal("%s", err);

    /* ---- EXEC: memory, image, relocations */
    cur = d;
    prog_layout = d->layout;
    cur_dgroup = SEG(d->dgroup_file);
    mem_place_image(&x);
    mem_free_exe(&x);
    codeptr_clear_abs();
    platform_prog_start();
    dos_arena_reset(PSP_SEG, MEM_TOP_SEG - 1);             /* EXEC gives the program all memory */
    memcpy(mp(PSP_SEG - 1, 8), d->mcb_name, strlen(d->mcb_name));

    char tail[128];
    snprintf(tail, sizeof tail, "%s%s -n%s\\", extra_args && *extra_args ? " " : "",
             extra_args ? extra_args : "", DOS_WORK_DIR);   /* GCD: tail + '\' */
    u16 env_bytes, env_strings;
    u16 path_ofs = build_env_and_psp(d, tail, &env_bytes, &env_strings);

    /* ---- Borland c0 (0000:0000, platform.md §2.3) */
    DSW(0x0092) = 0x0005;                                  /* _version: INT 21h/30h = DOS 5.00 */
    DSW(0x0090) = PSP_SEG;                                 /* _psp */
    DSW(0x008C) = ENV_SEG;                                 /* _envseg */
    DSW(0x00A8) = rd16(PSP_SEG, 0x02);                     /* top of memory */
    DSW(0x008A) = env_bytes;                               /* _envLng */
    DSW(0x008E) = (u16)((env_strings * 2 + 8) & 0xFFF8);   /* _envSize */
    u16 bp = (u16)(DSW(0x00A8) - cur_dgroup);
    if (DSW(d->stklen_ds) < 0x200) DSW(d->stklen_ds) = 0x200;
    u32 need = (u32)DSW(d->stklen_ds) + d->bss_end + DSW(d->heaplen_ds);
    if (need > 0xFFFF) host_fatal("%s: DGROUP too large", d->exe);   /* c0: "Not enough memory" */
    u16 di = (u16)((need >> 4) + 1);
    if (bp < di) host_fatal("%s: not enough memory", d->exe);
    if (DSW(d->stklen_ds) == 0 || DSW(d->heaplen_ds) == 0) di = bp > 0x1000 ? 0x1000 : bp;
    u16 top = (u16)(cur_dgroup + di);
    DSW(0x00A0) = top;                                     /* heap top segment */
    DSW(0x00A4) = top;
    if (!dos_mem_resize(PSP_SEG, (u16)(top - PSP_SEG), NULL))   /* INT 21h/4Ah */
        host_fatal("%s: cannot shrink the program block", d->exe);
    dg_sp = (u16)(di << 4);                                /* SS = DS; SP = di * 16 (0 = 10000h) */
    memset(DSP(d->bss_start), 0, (size_t)(d->bss_end - d->bss_start));   /* BSS */
    u32 t = rd32(0x40, 0x6C);                              /* INT 1Ah: ticks since midnight */
    DSW(0x0096) = (u16)t;
    DSW(0x0098) = (u16)(t >> 16);
    u16 argv_ds;
    int argc = build_argv(path_ofs, tail, &argv_ds);
    DSW(0x0084) = (u16)argc;
    DSW(0x0086) = argv_ds;
    DSW(0x0088) = 0;                                       /* envp: set by _setenvp (INTRO/PS) */
    atexit_count = 0;

    if (host_trace())
        fprintf(stderr, "[prog] %s: image at %04X, DGROUP %04X, top %04X, largest free %u bytes, tail \"%s\"\n",
                d->exe, LOAD_SEG, cur_dgroup, top, (unsigned)dos_mem_largest() * 16, tail);

    if (setjmp(exit_jmp) == 0) {
        rtl_c0_start();                                    /* c0 _StartTable (platform) */
        int rc = platform_dev_test();                      /* AOTP_TEST developer aids */
        if (rc < 0) rc = d->main(argc, argv_ds);
        prog_exit(rc);                                     /* c0: exit(main(...)) */
    }
    platform_prog_end();
    codeptr_clear_abs();
    cur = NULL;
    prog_layout = NULL;
    if (host_trace()) fprintf(stderr, "[prog] %s exit(%d)\n", d->exe, exit_code);
    return exit_code & 0xFF;
}

_Noreturn void prog_exit(int code)
{
    /* Borland exit(): atexit functions in reverse order, then _cleanup (#pragma exit: only AP's
     * overlay manager, dropped), (*_exitbuf)(), _restorezero, _checknull, (*_exitfopen)(),
     * (*_exitopen)() (platform rtl_c0_exit), then INT 21h/4Ch = back to the chain loop. */
    while (atexit_count > 0) {
        void (*fn)(void) = atexit_fns[--atexit_count];
        fn();
    }
    rtl_c0_exit();
    exit_code = code;
    longjmp(exit_jmp, 1);
}

int prog_atexit(void (*fn)(void))
{
    if (atexit_count >= 32) return 1;
    atexit_fns[atexit_count++] = fn;
    return 0;
}

bool prog_check_all(const char *flat_reference, char *err, size_t errlen)
{
    for (int id = PROG_INTRO; id <= PROG_AP; id++) {
        const ProgDesc *d = prog_desc(id);
        ExeImage x;
        if (!read_program(d, &x, err, errlen)) return false;
        printf("%-9s ok: image %05X bytes, %u relocations, DGROUP %04X (at %04X), BSS DS:%04X-%04X",
               d->exe, (unsigned)x.image_len, (unsigned)x.nrelocs, d->dgroup_file, SEG(d->dgroup_file),
               d->bss_start, d->bss_end - 1);
        if (d->flatten)
            printf(", %d overlay segments (%d entries) flattened from %05X",
                   x.ovl_segments, x.ovl_entries, (unsigned)x.ovl_start);
        printf("\n");
        if (d->flatten && flat_reference) {
            ExeImage r;
            if (!mem_read_exe(flat_reference, false, &r, err, errlen)) { mem_free_exe(&x); return false; }
            u32 diff = 0, rdiff = 0;
            bool same_len = r.image_len == x.image_len;
            for (u32 i = 0; same_len && i < x.image_len; i++) {
                diff += x.image[i] != r.image[i];
                rdiff += x.reloc_map[i] != r.reloc_map[i];
            }
            printf("          vs %s: %s, %u byte differences, %u relocation differences (%u relocs)\n",
                   flat_reference, same_len ? "same size" : "SIZE DIFFERS", (unsigned)diff, (unsigned)rdiff,
                   (unsigned)r.nrelocs);
            bool ok = same_len && diff == 0 && rdiff == 0;
            mem_free_exe(&r);
            if (!ok) { snprintf(err, errlen, "AP flattening differs from %s", flat_reference); mem_free_exe(&x); return false; }
        }
        mem_free_exe(&x);
    }
    return true;
}
