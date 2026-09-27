/* sim_flow: vcr_files.c — tape files: findfile.c (4bcd), modules.c (5155), vcr_file.c (5e06, the tape
 * picker, save / load / delete dialogs), vcr_list.c (5fef, the list box of _files_.out with its edit
 * field) — sim_flow_vcr.md §11. _files_.out records: char stem[28], char ext[4] at +1Ch. */
#include "sim_flow/sfb_priv.h"

#define LIST_SEG 0x3934                               /* lbox.BOX items at 3934:0024 (11 items) */

/* ---- 4bcd findfile.c ------------------------------------------------------------------------------ */

/* AP 4bcd:0000 write_name_rec(f, filename) */
void write_name_rec(u16 f, u16 name)
{
    u16 m = stk_mark();
    u16 rec = stk_alloc(0x20);
    memset(DSP(rec), 0, 0x20);
    u16 d = rec;
    while (DSB(name) != 0 && DSB(name) != '.') DSB(d++) = DSB(name++);
    if (DSB(name) == '.') rtl_strcpy((u16)(rec + 0x1C), (u16)(name + 1));
    rtl_fwrite(rec, 0x20, 1, f);
    stk_release(m);
}

/* AP 4bcd:0077 files_out_name() -> "_files_.out" */
u16 files_out_name(void)
{
    return 0x0254;
}

/* AP 4bcd:007f list_files(pattern, exclude) -> count: the matching names into _files_.out */
s16 list_files(u16 pattern, u16 exclude)
{
    u16 m = stk_mark();
    u16 path = stk_alloc(0x50), ff = stk_alloc(0x30);
    s16 n = 0;
    rtl_strcpy(path, 0x4032);
    rtl_strcat(path, files_out_name());
    u16 f = rtl_fopen(path, 0x0260 /* "wb" */);
    if (f == 0) assert_fail(far_make(SEG(0x3856), 0), 0x77, 0);
    rtl_strcpy(path, 0x4032);
    rtl_strcat(path, pattern);
    if (rtl_findfirst(path, ff, 0) == 0) {
        do {
            if (exclude == 0 || rtl_strcmp((u16)(ff + 0x1E), exclude) != 0) {
                write_name_rec(f, (u16)(ff + 0x1E));
                n++;
            }
        } while (rtl_findnext(ff) == 0);
    }
    rtl_fclose(f);
    stk_release(m);
    return n;
}

/* AP 4bcd:01ce list_files_all(pattern) */
s16 list_files_all(u16 pattern_ds)
{
    return list_files(pattern_ds, 0);
}

/* AP 4bcd:01df file_exists(path) */
s16 ap_file_exists(u16 path_ds)
{
    u16 f = my_fopen(path_ds, 0x05EB /* "rb" */);
    if (f == 0) return 0;
    my_fclose(f);
    return 1;
}

/* AP 4bcd:020c reserved_dos_name(s): CON AUX PRN NUL, or PRT / COM followed by a digit */
s16 reserved_dos_name(u16 s)
{
    if (rtl_strlen(s) < 3) return 0;
    if (rtl_strnicmp(0x0263, s, 3) == 0 || rtl_strnicmp(0x0267, s, 3) == 0 ||
        rtl_strnicmp(0x026B, s, 3) == 0 || rtl_strnicmp(0x026F, s, 3) == 0)
        return 1;
    if (rtl_strnicmp(0x0273, s, 3) == 0 || rtl_strnicmp(0x0277, s, 3) == 0)
        if (DSB(0x372F + (s16)DSC(s + 3)) & 2) return 1;
    return 0;
}

/* AP 4bcd:02c0 findfile_init() */
void findfile_init(void)
{
    DSB(0x3F28) = 1;
    DSW(0x3F26) = 4;
}

/* ---- 5155 modules.c ------------------------------------------------------------------------------- */

/* AP 5155:0000 module_block(block, mask): 40 bytes of M<k+1>SHL for every mask bit k */
void module_block(u16 block, u16 mask)
{
    memset(DSP(block), 0, 0x16C);
    DSB(block + 0x168) = (u8)mask;
    u8 digit = '1';
    for (u16 p = block; mask != 0; mask >>= 1, digit++, p = (u16)(p + 0x28)) {
        if (!(mask & 1)) continue;
        DSB(0x06FB) = digit;
        u16 f = my_fopen(res_name(0x06FA /* "M#SHL" */, 0x05BC), 0x0616 /* "rb" */);
        if (f == 0) continue;
        my_fread(p, 1, 0x28, f);
        my_fclose(f);
    }
}

/* AP 5155:0082 is_marked(rec) */
s16 is_marked(u16 rec_ds)
{
    return DSB(rec_ds) == '*';
}

/* AP 5155:0098 unmarked_name(dst, rec) */
u16 unmarked_name(u16 dst, u16 rec)
{
    rtl_strcpy(dst, is_marked(rec) ? (u16)(rec + 1) : rec);
    return dst;
}

/* AP 5155:00fa tape_version(block, name) -> header +02 (the film format) */
u16 tape_version(u16 block, u16 name)
{
    u16 m = stk_mark();
    u16 path = stk_alloc(0x92), tp = stk_alloc(0x28), hdr = stk_alloc(0x42);
    rtl_strcpy(path, 0x4032);
    rtl_strcat(path, make_filename(tape_path_to(tp, name), 0x060A /* "VCR" */));
    u16 f = rtl_fopen(path, 0x0619 /* "rb" */);
    if (f == 0) assert_fail(far_make(SEG(0x387F), 0), 0x65, path);
    s16 n = (s16)rtl_fread(hdr, 0x42, 1, f);
    n = (s16)(n + rtl_fread(block, 0x16C, 1, f));
    if (n != 2) assert_fail(far_make(SEG(0x387F), 0), 0x68, name);
    rtl_fclose(f);
    u16 v = DSW(hdr + 2);
    stk_release(m);
    return v;
}

/* AP 5155:0207 validate_tape_list() -> usable count: mark films of another version with '*' */
s16 validate_tape_list(void)
{
    u16 m = stk_mark();
    u16 path = stk_alloc(0x50), block = stk_alloc(0x16C), rec = stk_alloc(0x20);
    rtl_strcpy(path, 0x4032);
    modules_mask();
    rtl_strcat(path, files_out_name());
    u16 f = rtl_fopen(path, 0x061C /* "r+b" */);
    if (f == 0) assert_fail(far_make(SEG(0x387F), 0), 0x7E, 0);
    s16 valid = 0;
    u16 off = 0;
    while (rtl_fread(rec, 0x20, 1, f) == 1) {
        u8 mark;
        if (tape_version(block, rec) != 0x12) mark = '*';
        else { mark = 0; valid++; }
        if (mark) {
            rtl_fseek(f, (s32)(s16)off, 0);
            rtl_memmove((u16)(rec + 1), rec, (u16)(rtl_strlen(rec) + 1));
            DSB(rec) = mark;
            rtl_fwrite(rec, 0x20, 1, f);
            rtl_fseek(f, (s32)(u16)(off + 0x20), 0);
        }
        off = (u16)(off + 0x20);
    }
    rtl_fclose(f);
    stk_release(m);
    return valid;
}

/* AP 5155:0369 text_ids(list, group, first, n): far pointers to n STR texts */
void text_ids(u16 list, s16 group, s16 first, s16 n)
{
    for (s16 i = 0; i < n; i++) {
        FarPtr s = str(group, (s16)(first + i));
        DSW(list + 2) = s.seg;
        DSW(list) = s.off;
        list = (u16)(list + 4);
    }
}

/* AP 5155:03a4 explain_tape(name): which add-ons a tape needs (or that its version is wrong) */
void explain_tape(u16 name)
{
    u16 m = stk_mark();
    u16 block = stk_alloc(0x16C), list = stk_alloc(0x3C), code = stk_alloc(2);
    DSW(code) = 9;
    s16 k;
    if (tape_version(block, name) != 0x12) {
        k = 3;
        text_ids(list, 0, 0xBE, 3);
    } else {
        text_ids(list, 0, 0xC1, 5);
        u8 miss = (u8)(~modules_mask() & DSB(block + 0x168));
        k = 5;
        for (s16 i = 0; i < 8; i++, miss >>= 1) {
            if (miss & 1) {
                DSW(list + 4 * k + 2) = cur_dgroup;
                DSW(list + 4 * k) = (u16)(block + 0x28 * i);
                k++;
            }
        }
    }
    DSL(list + 4 * k) = 0;
    s16 n = alert_build(list, 0x060E, code);
    dlg_activate(far_make(SEG(0x38BF), 0x16), n);
    dlg_force_redraw();
    vcr_refresh_screen(0);
    DSB(0x3BB8) = 1;
    while (dlg_poll() != 9) {}
    stk_release(m);
}

/* ---- 5e06 vcr_file.c ------------------------------------------------------------------------------ */

/* AP 5e06:0000 tape_rename(from, to): both under the work directory */
void tape_rename(u16 from_ds, u16 to_ds)
{
    u16 m = stk_mark();
    u16 a = stk_alloc(0x50), b = stk_alloc(0x50);
    rtl_strcpy(a, 0x4032);
    rtl_strcat(a, from_ds);
    rtl_strcpy(b, 0x4032);
    rtl_strcat(b, to_ds);
    rtl_rename(a, b);
    stk_release(m);
}

/* AP 5e06:00ba tape_unlink(name) */
void tape_unlink(u16 name_ds)
{
    u16 m = stk_mark();
    u16 a = stk_alloc(0x50);
    rtl_strcpy(a, 0x4032);
    rtl_strcat(a, name_ds);
    rtl_unlink(a);
    stk_release(m);
}

/* AP 5e06:0120 modal_depth(d): the first modal level hides the panel region */
void modal_depth(s16 d)
{
    if (!DSB(0x08D5)) return;
    if (d == 1) {
        if (DSB(0x0AB8) == 0) {
            vcr_panel_region0();
            vcr_panel_poll_off();
            dlg_force_redraw();
        }
    } else if (DSB(0x0AB8) == 1) {
        vcr_panel_region();
        dlg_activate(far_make(SEG(0x38E6), 0x15), 0x25);
        vcr_panel_poll_on();
    }
    DSB(0x0AB8) = (u8)(DSB(0x0AB8) + d);
}

/* AP 5e06:0175 modal_enter() */
void modal_enter(void)
{
    modal_depth(1);
}

/* AP 5e06:0183 modal_leave() */
void modal_leave(void)
{
    modal_depth(-1);
}

/* AP 5e06:0191 tape_pick(title1, title2) -> DS:4E86 / 0 */
u16 tape_pick(s16 title1, s16 title2)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x28);
    s16 done = 0;
    u16 name = 0;
    modal_enter();
    sx_stop_all();
    s16 n = list_files_all(0x0AAC /* "TAPES\*.VCR" */);
    validate_tape_list();
    list_open(title1, title2, n);
    dlg_force_redraw();
    wipe_in(2);
    DSB(0x3BB8) = 1;
    while (!done) {
        s16 r = dlg_poll();
        if (r == 1) { done = 1; name = 0; }
        else if (r == 2) {
            name = unmarked_name(buf, list_edit_text());
            name = make_filename(name, 0x060A /* "VCR" */);
            if (ap_file_exists(tape_path(name))) {
                if (is_marked(list_edit_text())) {
                    explain_tape(name);
                    vcr_refresh_screen(0);
                    list_reinit();
                    dlg_force_redraw();
                    DSB(0x3BB8) = 1;
                    list_set_edit(0x0AB9);
                } else {
                    rtl_strcpy(0x4E86, name);
                    name = 0x4E86;
                    done = 1;
                }
            } else {
                list_set_edit(0x0ABA);
            }
        }
    }
    vcr_refresh_screen(0);
    wipe_in(2);
    modal_leave();
    stk_release(m);
    return name;
}

/* AP 5e06:02d9 director_mode(set) -> DS:088B (1 / 0 set it; other values only read) */
s16 director_mode(s16 set)
{
    if (set == 1) vcr_set_director(1);
    else if (set == 0) vcr_set_director(0);
    return (s16)(s8)DSB(0x088B);
}

/* AP 5e06:0304 tape_commit_copy() -> ok: copy the rest of the source film into the director film */
s16 tape_commit_copy(void)
{
    s16 once = 1, dec = 1;
    if (view_get() != 0 && view_get() != 0x18) dec = 0;
    if (DSSL(0x4E3B) < DSSL(0x50A2)) {
        do {
            if (once) {
                once = (s16)(once - dec);
                vcr_view(0x0918);
            }
        } while (vcr_play_frame(1, 0, 1, 1) == 0);
    }
    memcpy(DSP(0x506E), DSP(0x50B0), 0x1E);
    DSL(0x5060) = DSL(0x50A2);
    DSL(0x5064) = DSL(0x50A6);
    s16 r = vcr_finish_recording();
    if (r == 0) assert_fail(far_make(SEG(0x38E3), 0), 0x161, 0);
    return r;
}

/* AP 5e06:03a2 tape_save_changes() -> 0 cancel / 2 discarded / 3 saved */
u8 tape_save_changes(void)
{
    u8 r;
    modal_enter();
    if (!DSB(0x092F)) r = 2;
    else if (!DSB(0x0930)) { rec_abort(); r = 2; }
    else {
        s16 c = alert_modal(far_make(SEG(0x3959), 0), far_make(SEG(0x395A), 0));
        if (c == 1) {
            tape_commit_copy();
            play_close();
            tape_unlink(tape_path(0x4E04));
            u16 to = tape_path(0x4E04);
            tape_rename(tape_path(tape_tmp_name()), to);
            r = 3;
        } else if (c == 2) {
            rec_abort();
            r = 2;
        } else {
            r = 0;
        }
    }
    modal_leave();
    return r;
}

/* AP 5e06:044f tape_menu_load() -> another film was loaded */
u8 tape_menu_load(void)
{
    u8 done = 0, loaded = 0;
    while (!done) {
        u16 name = tape_pick(0x96, 0x95);
        if (name == 0) { done = 1; continue; }
        if (rtl_strcmp(name, 0x4E04) != 0) {
            s16 r = (s16)(s8)tape_save_changes();
            if (r != 0) {
                vcr_switch(name, r != 3);
                loaded = 1;
                done = 1;
            }
        } else {
            alert_modal(far_make(SEG(0x395B), 0), far_make(SEG(0x3957), 0));
        }
    }
    return loaded;
}

/* AP 5e06:04f4 tape_ask_name() -> DS:4EB2 / 0: the name for a new film */
u16 tape_ask_name(void)
{
    u16 m = stk_mark();
    u16 local = stk_alloc(0x18);
    u16 name = 0;
    u8 done = 0;
    modal_enter();
    s16 n = list_files_all(0x0AAC);
    list_open(0x91, 0x92, n);
    dlg_force_redraw();
    DSB(0x3BB8) = 1;
    wipe_in(2);
    while (!done) {
        s16 r = dlg_poll();
        if (r == 1) { name = 0; done = 1; }
        else if (r == 2) {
            rtl_strcpy(0x4EB2, make_filename(list_edit_text(), 0x060A /* "VCR" */));
            name = 0x4EB2;
            if (reserved_dos_name(0x4EB2)) {
                list_set_edit(0x0ABB);
                name = 0;
            } else if (ap_file_exists(tape_path(name))) {
                vcr_refresh_screen(0);
                if (alert_modal(far_make(SEG(0x395D), 0), far_make(SEG(0x395E), 0)) == 1) {
                    done = 1;
                } else {
                    list_reinit();
                    dlg_force_redraw();
                    DSB(0x3BB8) = 1;
                }
            } else {
                done = 1;
            }
        }
    }
    DSB(local) = 0;
    if (name) rtl_strcpy(local, name);               /* (a null name copies DS:0000, an empty string) */
    vcr_refresh_screen(0);
    wipe_in(2);
    if (name) rtl_strcpy(0x4E9C, name);
    modal_leave();
    if (name) rtl_strcpy(name, local);
    stk_release(m);
    return name;
}

/* AP 5e06:06a2 tape_menu_save() -> the new name / 0 */
u16 tape_menu_save(void)
{
    u16 name = 0;
    if (!DSB(0x092D)) {
        alert_modal(far_make(SEG(0x395F), 0), far_make(SEG(0x3957), 0));
    } else if (DSB(0x0930)) {
        name = tape_ask_name();
        if (name) {
            tape_commit_copy();
            play_close();
            if (ap_file_exists(tape_path(name))) tape_unlink(tape_path(name));
            u16 to = tape_path(name);
            tape_rename(tape_path(tape_tmp_name()), to);
            vcr_set_name(name);
            vcr_reopen(name);
        }
    } else {
        alert_modal(far_make(SEG(0x3960), 0), far_make(SEG(0x3957), 0));
    }
    return name;
}

/* AP 5e06:0755 list_other_tapes(pattern) -> count without the current film */
s16 list_other_tapes(u16 pattern)
{
    return list_files(pattern, 0x4E04);
}

/* AP 5e06:0768 tape_menu_delete() */
void tape_menu_delete(void)
{
    u16 m = stk_mark();
    u16 buf = stk_alloc(0x28);
    u8 done = 0;
    s16 n = list_other_tapes(0x0AAC);
    if (n == 0) {
        alert_modal(far_make(SEG(0x395C), 0), far_make(SEG(0x3957), 0));
        stk_release(m);
        return;
    }
    validate_tape_list();
    modal_enter();
    list_open(0x93, 0x94, n);
    dlg_force_redraw();
    DSB(0x3BB8) = 1;
    wipe_in(2);
    while (!done) {
        s16 r = dlg_poll();
        if (r == 1) done = 1;
        else if (r == 2) {
            unmarked_name(buf, list_edit_text());
            u16 name = make_filename(buf, 0x060A /* "VCR" */);
            if (ap_file_exists(tape_path(name))) {
                if (rtl_strcmp(name, 0x4E04) != 0) {
                    tape_unlink(tape_path(name));
                    n = list_other_tapes(0x0AAC);
                    validate_tape_list();
                    if (n == 0) done = 1;
                    else list_init(n);
                } else {
                    vcr_refresh_screen(0);
                    alert_modal(far_make(SEG(0x3961), 0), far_make(SEG(0x3957), 0));
                    list_set_edit(0x0ABC);
                    list_reinit();
                    dlg_force_redraw();
                    DSB(0x3BB8) = 1;
                }
            } else {
                list_set_edit(0x0ABD);
            }
        }
    }
    vcr_refresh_screen(0);
    wipe_in(2);
    modal_leave();
    stk_release(m);
}

/* AP 5e06:08e8 tape_menu_backdrop() */
void tape_menu_backdrop(void)
{
    read_front();
    write_back();
    gfx_copy_rect(0, 0, DSS(0x2DAA), DSS(0x2DAC));
}

/* AP 5e06:090a tape_menu() -> a film was (re)loaded: LOAD / SAVE / DELETE / EXIT (tapes.BOX) */
u8 tape_menu(void)
{
    u8 exit = 0, reloaded = 0, activate = 1;
    modal_enter();
    sx_stop_all();
    tape_menu_backdrop();
    while (!exit) {
        if (activate) {
            dlg_activate(far_make(SEG(0x38E6), 0x0343), 6);
            dlg_force_redraw();
            DSB(0x3BB8) = 1;
        }
        activate = 1;
        switch (dlg_poll() - 1) {
        case 0:
            vcr_refresh_screen(0);
            if (tape_menu_load()) { exit = 1; reloaded = 1; }
            break;
        case 1:
            vcr_refresh_screen(0);
            if (tape_menu_save()) { exit = 1; reloaded = 1; }
            break;
        case 2:
            vcr_refresh_screen(0);
            tape_menu_delete();
            break;
        case 3:
            vcr_refresh_screen(0);
            exit = 1;
            break;
        default:
            activate = 0;
            break;
        }
    }
    read_back();
    write_front();
    wipe_in(2);
    modal_leave();
    return reloaded;
}

/* ---- 5fef vcr_list.c ------------------------------------------------------------------------------ */

/* AP 5fef:0000 list_cb_scroll() — button callback: result = item +10h */
void list_cb_scroll(void)
{
    if (button_press(0)) DSW(0x3BC1) = IT_W(CUR_ITEM(), 0x10);
}

/* AP 5fef:001e list_scroll_arrows() — callback: item 5 up, else down (repeat every 3Bh ticks) */
void list_scroll_arrows(void)
{
    if (!button_style_b(far_make(0, 0))) return;
    s16 first = DSS(0x0B40), cur = DSS(0x4F05);
    if (!(STATE & 4)) {
        if (!timeout_expired(0x0154)) return;
    } else {
        timeout_start(0x0154, 0x3B);
    }
    if (CUR_IDX == 5) {
        if (DSS(0x4F05) > 0) DSW(0x4F05) -= 1;
        else if (DSS(0x0B40) > 0) DSW(0x0B40) -= 1;
    } else {
        if (DSS(0x4F01) - 1 > DSS(0x4F05)) DSW(0x4F05) += 1;
        else if (DSS(0x0B40) < DSS(0x4EFF)) DSW(0x0B40) += 1;
    }
    if (first != DSS(0x0B40) || cur != DSS(0x4F05)) {
        DSW(0x4F03) = (u16)(DSS(0x0B40) + DSS(0x4F05));
        list_read_selected();
        SEGB(LIST_SEG, 0x5C) = 0x43;
    }
}

/* AP 5fef:00cd list_row_at_mouse(cur, rowh) -> row (near) */
s16 list_row_at_mouse(s16 cur, s16 rowh)
{
    if (!(STATE & 0x0C)) return cur;
    s16 r = (s16)((s16)(DSS(0x3BBD) - (RY + 2)) / rowh);
    if (r < 0) return 0;
    if (r >= DSS(0x4F01)) r = (s16)(DSS(0x4F01) - 1);
    return r;
}

/* AP 5fef:0105 list_draw_rows() — callback: the visible rows of _files_.out, the cursor row lit */
void list_draw_rows(void)
{
    u16 m = stk_mark();
    u16 path = stk_alloc(0x50), rec = stk_alloc(0x20);
    s16 oldcur = DSS(0x4F05);
    rtl_strcpy(path, 0x4032);
    rtl_strcat(path, files_out_name());
    u16 f = rtl_fopen(path, 0x0B5A /* "rb" */);
    if (f == 0) f = rtl_fopen(files_out_name(), 0x0B5D);
    if (f == 0) assert_fail(far_make(SEG(0x3934), 0), 0xD0, 0);
    DSS(0x4F05) = list_row_at_mouse(DSS(0x4F05), 6);
    if (DSS(0x4F05) != oldcur) {
        DSW(0x4F03) = (u16)(DSS(0x0B40) + DSS(0x4F05));
        list_read_selected();
    }
    rtl_fseek(f, (s32)(u16)(DSW(0x0B40) << 5), 0);
    clear_26c3();
    inset_box();
    gfx_font_select(DSS(0x3F2E));
    DSB(0x26C2) = 1;
    s16 y = (s16)(RY + 4);
    for (s16 i = 0; i < DSS(0x4F01); i++, y = (s16)(y + 6)) {
        DSB(0x26C0) = 0xCC;
        if (i == DSS(0x4F05)) {
            DSB(0x26CC) = 1;
            DSB(0x26CE) = 0xB9;
            DSB(0x26CD) = 0xBE;
            fill_c1_item((s16)(RX + 2), (s16)(y - 1), (s16)(RW - 3), 7);
            DSB(0x26C0) = 0xCC;
        }
        rtl_fread(rec, 0x20, 1, f);
        gfx_text(rec, (s16)(RX + 0x0A), y);
    }
    rtl_fclose(f);
    FarPtr it = CUR_ITEM();
    wr8(it.seg, (u16)(it.off + 0x0C), 0x5C);
    stk_release(m);
}

/* AP 5fef:029f list_edit_text() -> DS:4EF0 */
u16 list_edit_text(void)
{
    return 0x4EF0;
}

/* AP 5fef:02a7 list_read_selected() (near): record DS:4F03 into the edit field */
void list_read_selected(void)
{
    if (DSS(0x4EFD) <= 0 || DSS(0x4F05) < 0) return;
    u16 m = stk_mark();
    u16 path = stk_alloc(0x50), rec = stk_alloc(0x20);
    rtl_strcpy(path, 0x4032);
    rtl_strcat(path, files_out_name());
    u16 f = rtl_fopen(path, 0x0B60 /* "rb" */);
    if (f == 0) f = rtl_fopen(files_out_name(), 0x0B63);
    if (f == 0) assert_fail(far_make(SEG(0x3934), 0), 0x143, 0);
    rtl_fseek(f, (s32)(u16)(DSW(0x4F03) << 5), 0);
    rtl_fread(rec, 0x20, 1, f);
    rtl_fclose(f);
    list_set_edit(rec);
    stk_release(m);
}
/* AP 5fef:02a8 */
void list_read_selected_body(void) { list_read_selected(); }

/* AP 5fef:03a5 list_set_edit(s) -> 1 / 0 (too long) */
s16 list_set_edit(u16 s)
{
    u16 len = rtl_strlen(s);
    if ((s16)len > DSS(0x0B44)) return 0;
    rtl_strcpy(0x4EF0, s);
    DSW(0x0B48) = len;
    DSW(0x0B46) = len;
    DSW(0x4EFB) = 1;
    SEGB(LIST_SEG, 0xCA) = 0x43;
    return 1;
}

/* AP 5fef:0405 is_name_char(c) (near) */
s16 is_name_char(s16 c)
{
    return (DSB(0x372F + c) & 0x0E) || c == '_';
}
/* AP 5fef:0406 */
s16 is_name_char_body(s16 c) { return is_name_char(c); }

/* AP 5fef:0424 edit_key(scan, ch) -> changed (near): insert up to 8 characters (9 with a '*'),
 * cursor right / left, backspace, delete */
s16 ap_edit_key(u16 scan, u16 ch)
{
    s16 changed = 0;
    if (memchr(DSP(0x0B4A), (u8)scan, 6)) {
        switch (scan) {
        case 0x0E:
            if (DSS(0x0B46) > 0) {
                rtl_memmove((u16)(DSW(0x0B46) + 0x4EEF), (u16)(DSW(0x0B46) + 0x4EF0), (u16)(DSW(0x0B48) - DSW(0x0B46) + 1));
                DSW(0x0B48) -= 1;
                DSW(0x0B46) -= 1;
                changed = 1;
            }
            break;
        case 0x4B:
            if (DSS(0x0B46) > 0) { DSW(0x0B46) -= 1; changed = 1; }
            break;
        case 0x4D:
            if (DSS(0x0B46) < DSS(0x0B48)) { DSW(0x0B46) += 1; changed = 1; }
            break;
        case 0x53:
            if (DSS(0x0B48) > 0 && DSS(0x0B46) < DSS(0x0B48)) {
                rtl_memmove((u16)(DSW(0x0B46) + 0x4EF0), (u16)(DSW(0x0B46) + 0x4EF1), (u16)(DSW(0x0B48) - DSW(0x0B46)));
                DSW(0x0B48) -= 1;
                changed = 1;
            }
            break;
        default:
            break;
        }
        return changed;
    }
    if (!is_name_char((s16)ch)) return 0;
    if (DSS(0x0B48) >= DSS(0x0B44)) return 0;
    if (DSS(0x0B48) >= 8) {
        if (DSS(0x0B48) != 8) return 0;
        if (!memchr(DSP(0x4EF0), '*', (size_t)(rtl_strlen(0x4EF0) + 1))) return 0;
    }
    rtl_memmove((u16)(DSW(0x0B46) + 0x4EF1), (u16)(DSW(0x0B46) + 0x4EF0), (u16)(DSW(0x0B48) - DSW(0x0B46) + 1));
    DSB(DSW(0x0B46) + 0x4EF0) = (u8)ch;
    DSW(0x0B46) += 1;
    DSW(0x0B48) += 1;
    return 1;
}
/* AP 5fef:0425 */
s16 edit_key_body(u16 scan, u16 ch) { return ap_edit_key(scan, ch); }

/* AP 5fef:058b list_draw_edit() — callback: the edit field with a blinking caret */
void list_draw_edit(void)
{
    gfx_font_select(DSS(0x3F2E));
    u16 caret = DSW(0x0B46);
    u8 c = DSB(0x4EF0 + caret);
    DSB(0x4EF0 + caret) = 0;
    s16 x = (s16)(RX + gfx_text_width(0x4EF0) + 0x0B);
    DSB(0x4EF0 + caret) = c;
    if (DSW(0x4EFB) != 0 || (STATE & 0x40)) {
        clear_26c3();
        inset_box();
        DSB(0x26C0) = 0xCC;
        DSB(0x26C2) = 1;
        gfx_text(0x4EF0, (s16)(RX + 0x0A), (s16)(RY + 4));
    }
    DSB(0x26CE) = DSW(0x0B42) ? 0xB9 : 0xC0;
    s16 y = (s16)(RY + 7 + 2);
    vline((s16)(x - 1), y, (s16)(x + 4), y);
    DSW(0x4EFB) = 1;
    FarPtr it = CUR_ITEM();
    wr8(it.seg, (u16)(it.off + 0x0C), 0x40);
}

/* AP 5fef:0655 list_edit_poll(key, ascii): keypad keys carry no character; caret blink */
void list_edit_poll(u16 key, u16 ascii)
{
    if (memchr(DSP(0x0B50), (u8)key, 10)) ascii = 0;
    if (ap_edit_key(key, ascii)) {
        DSW(0x4EFB) = 1;
        SEGB(LIST_SEG, 0xCA) = 0x43;
    }
    if (timeout_expired(0x015A)) {
        timeout_start(0x015A, 0x3B);
        DSW(0x0B42) ^= 1;
        DSW(0x4EFB) = 1;
    }
    if (DSW(0x4EFB) != 0) SEGB(LIST_SEG, 0xCA) = 0x43;
}

/* AP 5fef:06d5 list_edit_cb() — callback */
void list_edit_cb(void)
{
    list_edit_poll(DSW(0x3BBB), DSW(0x3BB9));
}

/* AP 5fef:06e9 list_clear_cb() — callback: C6h outline */
void list_clear_cb(void)
{
    DSB(0x26CC) = 0;
    DSB(0x26CE) = 0xC6;
    gfx_rect(RX, RY, RW, RH);
}

/* AP 5fef:0710 list_boxes_load(): lbox.BOX (3934:0024, 11 items), tapes.BOX (38E6:0343, 6 items) */
void list_boxes_load(void)
{
    box_load(far_make(SEG(LIST_SEG), 0x24), res_name(0x0B66 /* "lbox" */, 0x05C0 /* "BOX" */), 0x0B);
    box_load(far_make(SEG(0x38E6), 0x0343), res_name(0x0B6B /* "tapes" */, 0x05C0), 6);
}

/* AP 5fef:0757 list_init(n) */
void list_init(s16 n)
{
    DSS(0x4EFD) = n;
    DSW(0x4F05) = 0;
    DSW(0x4F03) = 0;
    DSW(0x0B40) = 0;
    DSS(0x4EFF) = (s16)(n - 0x0C);
    if (DSS(0x4EFF) < 0) DSW(0x4EFF) = 0;
    DSW(0x4F01) = (u16)(DSS(0x4EFD) - DSS(0x4EFF));
    timeout_start(0x015A, 0x3B);
    list_read_selected();
    SEGB(LIST_SEG, 0x5C) = 0x43;
}

/* AP 5fef:07ad list_reinit() */
void list_reinit(void)
{
    dlg_activate(far_make(SEG(LIST_SEG), 0x24), 0x0B);
}

/* AP 5fef:07c2 list_open(title1, title2, n) -> 1 */
s16 list_open(s16 title1, s16 title2, s16 n)
{
    SEGW(LIST_SEG, 0xF8) = (u16)title1;
    SEGW(LIST_SEG, 0x48) = (u16)title2;
    list_init(n);
    list_reinit();
    return 1;
}

__attribute__((constructor)) static void vcr_files_codeptrs(void)
{
    codeptr_register(PROG_AP, 0x5FEF0000u, list_cb_scroll);
    codeptr_register(PROG_AP, 0x5FEF001Eu, list_scroll_arrows);
    codeptr_register(PROG_AP, 0x5FEF0105u, list_draw_rows);
    codeptr_register(PROG_AP, 0x5FEF058Bu, list_draw_edit);
    codeptr_register(PROG_AP, 0x5FEF06D5u, list_edit_cb);
    codeptr_register(PROG_AP, 0x5FEF06E9u, list_clear_cb);
}
