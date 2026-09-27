/* sim_flow: vcr_tape.c (618d) — the tape file layer: mission script, film header, record/play streams,
 * the Huffman tables — sim_flow_vcr.md §5. Globals: DS:5010/5014 script far ptr / length, DS:5016 write
 * stream, DS:5018 its name, DS:502E read stream, DS:5030/5034 and DS:5038/503C the bit buffers,
 * DS:504A header being written, DS:508C header read. */
#include "sim_flow/sfb_priv.h"

#define VCR_TAPE_C far_make(SEG(0x394C), 0)          /* "d:\aces1946\code\sim\s\c\vcr_tape.c" */

/* AP 618d:0000 tape_path_to(dst, name) = "TAPES\" + name */
u16 tape_path_to(u16 dst_ds, u16 name_ds)
{
    memcpy(DSP(dst_ds), DSP(0x0C06), 7);              /* "TAPES\\" + NUL */
    rtl_strcat(dst_ds, name_ds);
    return dst_ds;
}

/* AP 618d:0043 tape_path(name): one of 4 rotating 22-byte buffers DS:4FA8 */
u16 tape_path(u16 name_ds)
{
    u16 i = DSW(0x0BDA) & 3;
    DSW(0x0BDA) = (u16)(i + 1);
    return tape_path_to((u16)(i * 0x16 + 0x4FA8), name_ds);
}

/* AP 618d:0067 load_script_file(file, &far ptr, &len) -> 1 */
s16 load_script_file(u16 file_ds, u16 p_ds, u16 n_ds)
{
    u16 f = my_fopen(file_ds, 0x05EB /* "rb" */);
    if (f == 0) assert_fail(VCR_TAPE_C, 0xAB, file_ds);
    DSW(0x5014) = (u16)my_filelength(f);
    ds_far_wr(0x5010, mem_far_new((u32)(s32)DSS(0x5014), 0));
    ap_fread_far_55a4(ds_far(0x5010), (s32)DSS(0x5014), f);
    my_fclose(f);
    DSL(p_ds) = DSL(0x5010);
    DSW(n_ds) = DSW(0x5014);
    return 1;
}

/* AP 618d:00fb free_script() -> 1 */
s16 free_script(void)
{
    mem_far_delete(ds_far(0x5010));
    DSW(0x5014) = 0;
    DSL(0x5010) = 0;
    return 1;
}

/* AP 618d:011f tape_read_script(name, &far ptr, &len) -> 1 (header -> DS:508C) */
s16 tape_read_script(u16 name_ds, u16 p_ds, u16 n_ds)
{
    u16 m = stk_mark();
    u16 blk = stk_alloc(0x16C);
    u16 f = my_fopen(tape_path(name_ds), 0x05EB /* "rb" */);
    if (f == 0) assert_fail(VCR_TAPE_C, 0xD8, name_ds);
    my_fread(0x508C, 0x42, 1, f);
    if (!(DSW(0x508C) & 1)) assert_fail(VCR_TAPE_C, 0xDC, name_ds);
    my_fread(blk, 0x16C, 1, f);
    my_fread(0x5014, 2, 1, f);
    ds_far_wr(0x5010, mem_far_new((u32)(s32)DSS(0x5014), 0));
    ap_fread_far_55a4(ds_far(0x5010), (s32)DSS(0x5014), f);
    my_fclose(f);
    DSL(p_ds) = DSL(0x5010);
    DSW(n_ds) = DSW(0x5014);
    stk_release(m);
    return 1;
}

/* AP 618d:0210 free_script2() -> 1 */
s16 free_script2(void)
{
    free_script();
    return 1;
}

/* AP 618d:021c alloc_bit_buffers() -> ok */
s16 alloc_bit_buffers(void)
{
    if (DSL(0x5030) == 0 && DSL(0x5038) == 0) {
        DSW(0x5034) = DSW(0x0BD6);
        DSW(0x503C) = DSW(0x0BD6);
        ds_far_wr(0x5030, mem_far_new((u32)DSW(0x0BD6), 0));
        ds_far_wr(0x5038, mem_far_new((u32)DSW(0x503C), 0));
    }
    return !(DSL(0x5030) == 0 || DSL(0x5038) == 0);
}

/* AP 618d:0281 free_bit_buffers() — no callers */
void free_bit_buffers(void)
{
    mem_far_delete(ds_far(0x5030));
    mem_far_delete(ds_far(0x5038));
    DSW(0x5034) = 0;
    DSW(0x503C) = 0;
    DSL(0x5030) = 0;
    DSL(0x5038) = 0;
}

/* AP 618d:02c4 alloc_decode_buffer(size) */
void alloc_decode_buffer(s16 size)
{
    if (size < 0x15) size = 0x14;
    DSS(0x5046) = size;
    DSW(0x5044) = mem_new((u16)size, 0, far_to_near_str(VCR_TAPE_C), 0x141);
}

/* AP 618d:0300 tape_wopen(name) -> ok */
s16 tape_wopen(u16 name_ds)
{
    rtl_strcpy(0x5018, name_ds);
    DSW(0x5016) = bs_open(tape_path(name_ds), 2, ds_far(0x5038), (u32)DSW(0x503C));
    return DSW(0x5016) != 0;
}

/* AP 618d:0358 tape_wclose(commit) -> ok */
s16 tape_wclose(s16 commit)
{
    s16 r = 1;
    if (DSW(0x5016) != 0) {
        r = commit ? bs_close(DSW(0x5016)) : bs_abort(DSW(0x5016));
        DSW(0x5016) = 0;
    }
    return r;
}

/* AP 618d:0393 tape_ropen(name) -> ok */
s16 tape_ropen(u16 name_ds)
{
    DSW(0x502E) = bs_open(tape_path(name_ds), 1, ds_far(0x5030), (u32)DSW(0x5034));
    return DSW(0x502E) != 0;
}

/* AP 618d:03c8 tape_rclose() */
void tape_rclose(void)
{
    bs_close(DSW(0x502E));
    DSW(0x502E) = 0;
}

/* AP 618d:03e5 put_bytes(far p, n, s) -> ok */
s16 put_bytes(FarPtr p, s16 n, u16 s)
{
    u16 m = stk_mark();
    u16 b = stk_alloc(2);
    s16 r = 1;
    while (n != 0) {
        DSB(b) = mem[far_lin(p)];
        p = far_add(p, 1);
        n--;
        if (bs_write_bits(b, 8, s) == 0) { r = 0; break; }
    }
    stk_release(m);
    return r;
}

/* AP 618d:0430 rec_begin(name) -> ok: header (stale +16..), zero module block, script */
s16 rec_begin(u16 name_ds)
{
    s16 r = tape_wopen(name_ds);
    if (r == 0) return 0;
    u16 m = stk_mark();
    u16 blk = stk_alloc(0x16C);
    memset(DSP(blk), 0, 0x16C);
    DSW(0x5042) = 0x0F;
    DSW(0x504A) = 0;
    DSW(0x504C) = 0x12;
    DSW(0x504E) = (u16)slider_value(0);
    memcpy(DSP(0x5050), DSP(AP_DS_realism_prf), 16);
    r &= bs_write_bits(0x504A, 0x210, DSW(0x5016));
    r &= bs_write_bits(blk, 0xB60, DSW(0x5016));
    r &= bs_write_bits(0x5014, 0x10, DSW(0x5016));
    r &= put_bytes(ds_far(0x5010), DSS(0x5014), DSW(0x5016));
    stk_release(m);
    return r;
}

/* AP 618d:04e4 module_flags(bits): bits != 0 -> DS:0BDC | bits (not stored: verified quirk);
 * bits == 0 -> DS:0BDC, then cleared */
u16 module_flags(u16 bits)
{
    if (bits) return (u16)(bits | DSW(0x0BDC));
    u16 r = DSW(0x0BDC);
    DSW(0x0BDC) = 0;
    return r;
}

/* AP 618d:0502 rec_finish() -> ok: close the write stream, patch the header and the module block */
s16 rec_finish(void)
{
    if (tape_wclose(1) == 0) return 0;
    u16 f = my_fopen(tape_path(0x5018), 0x0C0D /* "r+b" */);
    if (f == 0) return 0;
    u16 m = stk_mark();
    u16 hdr = stk_alloc(0x42), blk = stk_alloc(0x16C);
    s16 rd = my_fread(hdr, 0x42, 1, f);
    rd = (s16)(rd + my_fread(blk, 0x16C, 1, f));
    my_fseek(f, 0, 0);
    DSW(hdr) |= 1;
    DSL(hdr + 0x16) = DSL(0x5060);
    DSL(hdr + 0x1A) = DSL(0x5064);
    memcpy(DSP(hdr + 0x24), DSP(0x506E), 0x1E);
    s16 wr = my_fwrite(hdr, 0x42, 1, f);
    module_block(blk, (u16)(DSB(hdr + 0x2B) | module_flags(0)));
    wr = (s16)(wr + my_fwrite(blk, 0x16C, 1, f));
    s16 c = my_fclose(f);
    stk_release(m);
    return rd == 2 && wr == 2 && c != -1;
}

/* AP 618d:0629 rec_abort() -> tape_wclose(0); unlink the file */
s16 rec_abort(void)
{
    s16 r = tape_wclose(0);
    rtl_unlink(tape_path(0x5018));
    return r;
}

/* AP 618d:0653 play_read_header() -> 1 (near): header, module block, realism, slider 0, skip script */
s16 play_read_header(void)
{
    u16 m = stk_mark();
    u16 hdr = stk_alloc(0x42), blk = stk_alloc(0x16C), n = stk_alloc(2), b = stk_alloc(2);
    DSW(0x5040) = 0x0F;
    bs_read_bits(hdr, 0x210, DSW(0x502E));
    DSW(0x51EC) = (DSB(hdr + 0x2B) & 1) ? 1 : 0;
    bs_read_bits(blk, 0xB60, DSW(0x502E));
    memcpy(DSP(AP_DS_realism_prf), DSP(hdr + 6), 16);
    slider_set(0, DSS(0x5090));                       /* sic: the header read by 618d:011f */
    time_scale_apply();
    if (!(DSW(hdr) & 1)) assert_fail(VCR_TAPE_C, 0x21F, 0);
    bs_read_bits(n, 0x10, DSW(0x502E));
    while (--DSS(n) >= 0) bs_read_bits(b, 8, DSW(0x502E));
    memcpy(DSP(0x508C), DSP(hdr), 0x42);
    stk_release(m);
    return 1;
}
/* AP 618d:0654 */
s16 play_read_header_body(void) { return play_read_header(); }

/* AP 618d:0726 play_open(name) -> ok */
s16 play_open(u16 name_ds)
{
    s16 r = tape_ropen(name_ds);
    if (r) play_read_header();
    return r;
}

/* AP 618d:0742 play_rewind() -> 1 */
s16 play_rewind(void)
{
    bs_rewind(DSW(0x502E));
    play_read_header();
    return 1;
}

/* AP 618d:0759 play_close() */
void play_close(void)
{
    tape_rclose();
}

/* AP 618d:0768 huff_table(name) -> table (fatal when missing) */
u16 huff_table(u16 name_ds)
{
    u16 path = res_name(name_ds, 0x0BDE /* "HUF" */);
    u16 t = huff_load(path);
    if (t == 0) assert_fail(VCR_TAPE_C, 0x298, path);
    return t;
}

/* AP 618d:07b3 load_huff_tables() */
void load_huff_tables(void)
{
    DSW(0x500A) = huff_table(0x0BE2);                 /* BTN */
    DSW(0x5006) = huff_table(0x0BE6);                 /* JOY */
    DSW(0x5002) = huff_table(0x0BEA);                 /* PAN */
    DSW(0x5000) = huff_table(0x0BEE);                 /* RUD */
    DSW(0x500C) = huff_table(0x0BF2);                 /* SIM */
    DSW(0x500E) = huff_table(0x0BF6);                 /* SOUT */
    DSW(0x5008) = huff_table(0x0BFB);                 /* TICKS */
    DSW(0x5004) = huff_table(0x0C01);                 /* VIEW */
}

/* AP 618d:0820 vcr_tape_exit() — empty */
void vcr_tape_exit(void)
{
}
