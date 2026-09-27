/* Developer tests of the platform module (AOTP_TEST=...), run by prog_run in place of the program's
 * main after the c0 start-up (PORT: test scaffolding, not in the original). The program's own volume
 * set is opened (vol_init with its main's arguments), then:
 *   volumes  every entry of every *.DYN of the game folder is opened by name through the volume layer
 *            (my_fopen) and its bytes compared with the host's copy of the entry;
 *   chunks   every packed leaf chunk (tools/dynchunk.py PACKED) of every entry the program can open
 *            is found with chunk_find and decompressed with pk_open/pk_read: size and CRC-32 printed
 *            ("CHUNK vol entry path occurrence size crc"), comparable with the dynchunk.py output;
 *   loaders  every *.PAL / *.FNT / *.BMP / *.SCR entry is loaded and freed (needs the video driver).
 * Results go to stdout; the process then exits (0 = all passed, 1 = failures) without chaining. */
#include "platform/rtl_priv.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "prog.h"
#include "video/video.h"

typedef struct {
    char vol[16], name[16];
    u32 size;
    const u8 *data;
} Ent;

static Ent *ents;
static int n_ents;
static u8 *vol_bufs[32];
static int n_vols;

static u32 crc32_buf(u32 crc, const u8 *p, size_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static u16 le16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

/* the host's view of the volumes (dynvol.py) */
static void load_host_volumes(void)
{
    const char *dir = host_game_dir();
    DIR *d = opendir(dir);
    if (!d) return;
    char *names[32];
    int nn = 0;
    struct dirent *e;
    while ((e = readdir(d)) && nn < 32) {
        size_t l = strlen(e->d_name);
        if (l > 4 && !strcasecmp(e->d_name + l - 4, ".DYN")) names[nn++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)nn, sizeof names[0], cmp_str);
    for (int v = 0; v < nn; v++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, names[v]);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        u8 *b = malloc((size_t)sz);
        if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); continue; }
        fclose(f);
        vol_bufs[n_vols++] = b;
        u16 count = le16(b + 0x20);
        u32 off = 0;
        ents = realloc(ents, sizeof(Ent) * (size_t)(n_ents + count));
        for (int i = 0; i < count; i++) {
            off += 4u * le16(b + 0x24 + 4 * i + 2);
            Ent *en = &ents[n_ents++];
            snprintf(en->vol, sizeof en->vol, "%s", names[v]);
            memcpy(en->name, b + off, 13);
            en->name[13] = 0;
            en->size = le32(b + off + 13);
            en->data = b + off + 17;
        }
        free(names[v]);
    }
}

static void open_program_volumes(void)
{
    switch (prog_current_id()) {
    case PROG_INTRO: vol_init(0, 0x4000, 1); break;
    case PROG_PS:    vol_init(0, 0x4000, 0x0FFF); break;
    default:         DSW(0x1EBC) = 0x0583; vol_init(0, 0, 0); break;
    }
}

/* open an entry through the volume layer; 1 when the bytes are this entry's */
static int open_same(const Ent *e, u16 *fp)
{
    u16 m = stk_mark();
    u16 f = my_fopen(ds_lit(e->name), ds_lit("rb"));
    stk_release(m);
    *fp = f;
    if (!f) return 0;
    FarPtr buf = dos_alloc(e->size ? e->size : 1, 0, 0);
    if (far_is_null(buf)) host_fatal("devtest: no memory for %s", e->name);
    s32 got = my_hread(buf, (s32)e->size, 1, f);
    int same = got == 1 && memcmp(mp(buf.seg, 0), e->data, e->size) == 0;
    if (e->size == 0) same = 1;
    dos_free(buf);
    my_rewind(f);
    return same;
}

static int test_volumes(void)
{
    int ok = 0, other = 0, missing = 0;
    for (int i = 0; i < n_ents; i++) {
        u16 f;
        int same = open_same(&ents[i], &f);
        if (f) my_fclose(f);
        if (!f) { missing++; printf("VOL %s %s: not found\n", ents[i].vol, ents[i].name); }
        else if (!same) { other++; printf("VOL %s %s: another copy opened\n", ents[i].vol, ents[i].name); }
        else ok++;
    }
    printf("volumes: %d entries, %d opened with the right bytes, %d another copy, %d not found\n",
           n_ents, ok, other, missing);
    return 0;
}

/* ---- chunks */
static const char *const packed_paths[] = {
    "BMP:BIN:", "BMP:VGA:", "SCR:BIN:", "SCR:VGA:", "TT3:", "SSM:SNG:",
    "OVL:VGA:", "OVL:EGA:", "OVL:MCG:", "OVL:CGA:", "OVL:TAN:", "OVL:HEG:", "OVL:EVG:", "OVL:EVA:",
    "SSM:M32:", "SSM:NLD:", "SSM:ADL:", "SSM:TAN:", "SSM:PS1:", "SSM:STD:", "SSM:001:", "SSM:003:",
    "SSM:101:", NULL };

static bool is_packed(const char *p)
{
    for (int i = 0; packed_paths[i]; i++) if (!strcmp(p, packed_paths[i])) return true;
    return false;
}
static bool is_tag(const u8 *b)
{
    if (b[3] != ':') return false;
    for (int i = 0; i < 3; i++) if (b[i] < 0x20 || b[i] >= 0x7F) return false;
    return true;
}

typedef struct { char path[32]; u32 unpacked; } Leaf;

/* dynchunk.parse: false when the bytes are not a chunk list */
static bool parse(const u8 *d, u32 off, u32 end, const char *prefix, Leaf *out, int *n, int max)
{
    while (off < end) {
        if (off + 8 > end || !is_tag(d + off)) return false;
        u32 sz = le32(d + off + 4);
        bool cont = sz >> 31;
        sz &= 0x7FFFFFFFu;
        if (off + 8 + sz > end) return false;
        char path[64];
        snprintf(path, sizeof path, "%s%.4s", prefix, (const char *)(d + off));
        if (cont) {
            if (!parse(d, off + 8, off + 8 + sz, path, out, n, max)) return false;
        } else if (is_packed(path) && sz >= 5 && *n < max) {
            snprintf(out[*n].path, sizeof out[*n].path, "%s", path);
            out[*n].unpacked = le32(d + off + 9);
            (*n)++;
        }
        off += 8 + sz;
    }
    return true;
}

static int test_chunks(void)
{
    int total = 0, bad = 0, skipped = 0;
    static Leaf leaves[4096];
    for (int i = 0; i < n_ents; i++) {
        const Ent *e = &ents[i];
        int nl = 0;
        if (e->size < 8 || !is_tag(e->data)) continue;
        if (!parse(e->data, 0, e->size, "", leaves, &nl, 4096) || !nl) continue;
        u16 f;
        int same = open_same(e, &f);
        if (f) my_fclose(f);
        if (!same) { skipped += nl; printf("SKIP %s %s (%d packed chunks: %s)\n", e->vol, e->name, nl,
                                          f ? "another copy" : "not in this program's volumes"); continue; }
        u16 m = stk_mark();
        u16 h = chunk_open(ds_lit(e->name));
        for (int k = 0; k < nl; k++) {
            int occ = 0;
            for (int j = 0; j <= k; j++) if (!strcmp(leaves[j].path, leaves[k].path)) occ++;
            u16 mk = stk_mark();
            s32 pos = chunk_find(h, ds_lit(leaves[k].path), (s16)occ);
            u32 size = 0, crc = 0;
            bool fail = pos == -1;
            if (!fail) {
                s16 id = pk_open(0, h, ds_lit("r"), (u32)chunk_size(h));
                fail = id < 0;
                if (!fail) {
                    FarPtr buf = dos_alloc(0x8000, 0, 0);
                    u16 got;
                    do {
                        got = pk_read(id, buf, 0x7FFF);
                        crc = crc32_buf(crc, mp(buf.seg, 0), got);
                        size += got;
                    } while (got == 0x7FFF);
                    dos_free(buf);
                    pk_close(id);
                }
            }
            stk_release(mk);
            total++;
            if (fail || size != leaves[k].unpacked) bad++;
            printf("CHUNK %s %s %s %d %u %08x%s\n", e->vol, e->name, leaves[k].path, occ, (unsigned)size,
                   (unsigned)crc, fail ? " FAIL" : size != leaves[k].unpacked ? " SHORT" : "");
        }
        chunk_close(h);
        stk_release(m);
    }
    printf("chunks: %d packed chunks decompressed, %d failed or short, %d skipped\n", total, bad, skipped);
    return bad != 0;
}

/* ---- loaders */
static bool ext_is(const char *n, const char *x)
{
    size_t l = strlen(n);
    return l > 4 && !strcasecmp(n + l - 4, x);
}

static int test_loaders(void)
{
    int n[4] = { 0 }, bad[4] = { 0 };
    for (int i = 0; i < n_ents; i++) {
        const Ent *e = &ents[i];
        u16 f;
        int same = open_same(e, &f);
        if (f) my_fclose(f);
        if (!same) continue;
        u16 m = stk_mark();
        u16 nm = ds_lit(e->name);
        if (ext_is(e->name, ".PAL")) {
            u32 p = pal_load(nm);
            n[0]++;
            if (!p) { bad[0]++; printf("PAL %s: failed\n", e->name); }
            else { printf("PAL %s: %08x crc %08x\n", e->name, (unsigned)p,
                          (unsigned)crc32_buf(0, mp((u16)(p >> 16), (u16)p), LW(G_pal_size))); pal_free(far_make((u16)(p >> 16), (u16)p)); }
        } else if (ext_is(e->name, ".FNT")) {
            u16 s = font_load(nm);
            n[1]++;
            if (!s) { bad[1]++; printf("FNT %s: failed\n", e->name); }
            else {
                printf("FNT %s: slot %u kind %u w %u h %u first %u count %u\n", e->name, s,
                       DSB((u16)(lds(G_font_kind) + s)), DSB((u16)(lds(G_font_w) + s)),
                       DSB((u16)(lds(G_font_h) + s)), DSB((u16)(lds(G_font_first) + s)),
                       DSB((u16)(lds(G_font_count) + s)));
                font_free(s);
            }
        } else if (ext_is(e->name, ".BMP")) {
            u16 b = bmp_load(nm);
            n[2]++;
            if (!b) { bad[2]++; printf("BMP %s: failed\n", e->name); }
            else {
                u16 fr = DSW(b);
                printf("BMP %s: %d frames, kind %d, %ux%u\n", e->name, bmp_count(b), DSS((u16)(fr + 4)),
                       DSW((u16)(fr + 6)), DSW((u16)(fr + 8)));
                bmp_free(b);
            }
        } else if (ext_is(e->name, ".SCR")) {
            scr_load(nm, 0);
            n[3]++;
            printf("SCR %s: loaded\n", e->name);
        }
        stk_release(m);
    }
    printf("loaders: PAL %d (%d failed), FNT %d (%d failed), BMP %d (%d failed), SCR %d\n",
           n[0], bad[0], n[1], bad[1], n[2], bad[2], n[3]);
    return bad[0] + bad[1] + bad[2] != 0;
}

int shell_data_dev_test(void);

int sim_world_a_dev_test(void);   /* sim_world/swa_test.c: fm, ballistics */
int sim_world_b_dev_test(void);   /* sim_world/swb_test.c: ai */
int render3d_dev_test(void);      /* render3d/r3d_test.c: shapes */

int platform_dev_test(void)
{
    const char *t = getenv("AOTP_TEST");
    if (!t || !*t) return -1;
    if (!strcmp(t, "fm") || !strcmp(t, "ballistics") || !strcmp(t, "ai") || !strcmp(t, "shapes")) {
        /* AP module tests (run with --start 3) */
        int v = !strcmp(t, "ai") ? sim_world_b_dev_test()
              : !strcmp(t, "shapes") ? render3d_dev_test() : sim_world_a_dev_test();
        fflush(stdout);
        exit(v < 0 ? 1 : v);
    }
    if (!strcmp(t, "missions")) {                     /* shell_data (run with --start 2) */
        int v = shell_data_dev_test();
        fflush(stdout);
        exit(v < 0 ? 1 : v);
    }
    if (strcmp(t, "volumes") && strcmp(t, "chunks") && strcmp(t, "loaders") && strcmp(t, "pictures") &&
        strcmp(t, "ttm"))
        return -1;
    load_host_volumes();
    open_program_volumes();
    int r = 0;
    if (!strcmp(t, "volumes")) r = test_volumes();
    else if (!strcmp(t, "chunks")) r = test_chunks();
    else if (!strcmp(t, "loaders")) {
        s16 v = gfx_init(8, 0xFF, ds_lit("vmcode.ovl"));
        printf("gfx_init: %d\n", v);
        r = test_loaders();
    } else {
        int v = video_dev_test();                     /* pictures, ttm (video; opens nothing twice:
                                                         vol_init returns when already done) */
        if (v < 0) return -1;
        r = v;
    }
    fflush(stdout);
    exit(r);
}
