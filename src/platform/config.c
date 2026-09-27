/* PS configuration file helpers (platform.md §2.8): file.c 1193:0115..0856. */
#include "platform/rtl_priv.h"

#include "shell_ui/shell_ui.h"
#include "symbols_ps.h"

#define PS_name_buf   0x4B72   /* make_name's result buffer */
#define PS_file_c     0x4BE1   /* seg of "file.c" (assert file name, 4BE1:0000) */

/* PS 1193:0115 make_name(base, n, ext): DS:4B72 = base + itoa(n) (n > -1) + ext (res_add_ext) */
u16 make_name(u16 p0_ds, s16 a1, u16 p2_ds)
{
    u16 m = stk_mark();
    rtl_strcpy(PS_name_buf, p0_ds);
    if (a1 > -1) {
        u16 t = stk_alloc(6);
        rtl_itoa(a1, t, 10);
        rtl_strcat(PS_name_buf, t);
    }
    rtl_strcpy(PS_name_buf, res_add_ext(PS_name_buf, p2_ds));
    stk_release(m);
    return PS_name_buf;
}

/* PS 1193:01b7 file_read(name, buf, size): my_fopen(name, "rb") (no work_dir prefix), bytes read */
s16 file_read(u16 p0_ds, u16 p1_ds, s16 a2)
{
    u16 f = my_fopen(p0_ds, 0x0BFB /* "rb" */);
    if (!f) return 0;
    s16 n = my_fread(p1_ds, 1, a2, f);
    my_fclose(f);
    return n;
}

/* PS 1193:01ff file_write(name, buf, size): RTL fopen(work_dir + name, "wb") (assert file.c 0F5h) */
void file_write(u16 p0_ds, u16 p1_ds, s16 a2)
{
    u16 m = stk_mark();
    u16 path = stk_alloc(0x50);
    rtl_strcpy(path, 0x40CA /* work_dir */);
    rtl_strcat(path, p0_ds);
    u16 f = rtl_fopen(path, 0x0BFE /* "wb" */);
    if (!f) assert_fail(far_make(SEG(PS_file_c), 0), 0xF5, p0_ds);
    my_fwrite(p1_ds, a2, 1, f);
    my_fclose(f);
    stk_release(m);
}

/* PS 1193:0818 realism_load(name, buf): 16 bytes, defaults when missing */
void realism_load(u16 p0_ds, u16 p1_ds)
{
    if (!file_read(p0_ds, p1_ds, 16)) real_apply_preset(p1_ds, 0);
}

/* PS 1193:0840 realism_save(name, buf) */
void realism_save(u16 name_ds, u16 buf_ds) { file_write(name_ds, buf_ds, 16); }

/* PS 1193:0856 realism_save_current: career (SIM.IN mode 1) -> creal.prf, else mreal.prf */
void realism_save_current(void)
{
    bool career = DSB(0x4B87) == 1;
    realism_save(career ? 0x0C7F /* "creal.prf" */ : 0x0C89 /* "mreal.prf" */, career ? 0x47A9 : 0x4357);
}
