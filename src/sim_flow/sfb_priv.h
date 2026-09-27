#pragma once
/* sim_flow part B private header (VCR, dialog library, preferences, realism, sliders, stdalert,
 * bstream): the part-A private header plus the dialog-library state and the declarations of part-B
 * functions that sim_flow.h does not list (file-local ones shared between part-B files). */
#include "sim_flow/sfa_priv.h"

/* ---- dialog library state (sim_flow_ui.md §6) ---- */
#define RX        DSS(0x3BC9)               /* current item rectangle */
#define RY        DSS(0x3BC7)
#define RW        DSS(0x3BC5)
#define RH        DSS(0x3BC3)
#define STATE     DSW(0x3BCB)               /* event bits of the item being dispatched */
#define CUR_IDX   DSS(0x3BCD)
#define CUR_ITEM() ds_far(0x3BCF)
#define IT_W(it, o) rd16((it).seg, (u16)((it).off + (o)))
#define IT_S(it, o) ((s16)rd16((it).seg, (u16)((it).off + (o))))
#define IT_B(it, o) rd8((it).seg, (u16)((it).off + (o)))
/* item i (0x16 bytes) of a far table at SSSS:OOOO */
#define ITEM(fseg, off, i) far_make(SEG(fseg), (u16)((off) + (i) * 0x16))

#include <stdlib.h>

/* video/gfx_init.c: AP 244d:0170 (no iface in modules.csv, so not in video.h) */

/* bstream.c */
void bs_file_fill(u16 s, FarPtr cache);
void bs_fill_cache(u16 s);
void bs_debug_log(u16 s_ds);
s16  bs_file_append(u16 s, FarPtr buf, s16 n);
void bs_cache_put(FarPtr src, s16 n, u16 s, s16 final);
s16  bs_flush(u16 s, s16 final);
s16  bs_cache_get(FarPtr dst, s32 n, u16 s);
s16  bs_refill(u16 s);
void bs_compact(u16 s);
s16  huff_depth(u16 t, s16 i, FarPtr nodes);
s16  huff_save(u16 path_ds, u16 t);
u16  huff_alloc(s16 n);
void huff_free(u16 t);
s16  huff_add(u16 t, s16 sym, s32 freq);
s16  huff_build(u16 t);
s16  huff_find(u16 t, s16 sym, u16 idx_ds);

/* vcr_codec.c */
s16  huff_get(u16 tab);
s16  huff_put(u16 tab, s16 sym);
u16  sext(u16 v, s16 bits);
s16  view_encode(u16 v);
s16  view_encode_body(u16 v);
s16  view_decode(u16 v);
s16  view_decode_body(u16 v);
s16  throttle_encode(u16 rec);
s16  ctrl_encode(u16 rec);
s16  ctrl_encode_body(u16 rec);
s16  ctrl_decode(u16 rec);
s16  ctrl_decode_body(u16 rec);
s16  put_type(s16 type);
s16  put_type_body(s16 type);
s16  get_type(void);
s16  get_type_body(void);

/* vcr_tape.c */
void free_bit_buffers(void);
s16  tape_wopen(u16 name_ds);
s16  tape_wclose(s16 commit);
s16  tape_ropen(u16 name_ds);
void tape_rclose(void);
s16  put_bytes(FarPtr p, s16 n, u16 s);
s16  play_read_header(void);
s16  play_read_header_body(void);
u16  huff_table(u16 name_ds);

/* dlglib.c */
void far_text_draw(FarPtr s, s16 x, s16 y);
void item_rect(FarPtr items, s16 i);
s16  mouse_hide_over(void);
void frame_corners(s16 xs, s16 x, s16 y, s16 n, s16 step, u16 col);
void frame_edges(s16 hh);
void frame_part_a(s16 d, s16 top);
void frame_part_b(void);
void frame_panel(void);
void groove_box(s16 flat);
void ap_tile_fill(s16 x, s16 y, s16 w, s16 h);
void fill_rect(void);
void fill_rect2(void);
void frame_lines(s16 x0, s16 y0, s16 x1, s16 ytop, s16 bx0, s16 yside, s16 bx1, s16 ybot);
void button_draw(s16 down);
void button_up_b(void);
void button_down_b(void);

/* dialogs.c */
void textbox_init(void);
void textbox_draw(u8 flag, u16 s);
void textbox_cb(s8 kind, u16 s);
FarPtr dlg_items(void);
s16  dlg_count(void);
void focus_prev(void);
void focus_prev_body(void);
void focus_next(void);
s16  focus_move(void);
s16  focus_move_body(void);
void focus_draw(s16 x, s16 y, s16 delta);
void focus_draw_body(s16 x, s16 y, s16 delta);
s16  dlg_item_count(void);
u16  text_edge_pass(u8 ch, s16 px, s16 py);
void hline(void);
void arrow_button(u8 down);
void radio_button(void);
void check_button(void);
void lib2_1a40(s16 i);
void lib2_1a76(u8 col, u16 first);
void lib2_frame(s16 dx, u8 pressed);
void lib2_cb_a(void);
void lib2_cb_b(void);

/* sliders.c */
s16  slider_alloc(s16 i);
s16  slider_map(s16 v, s16 a0, s16 a1, s16 b0, s16 b1);
s16  slider_drawn_value(s16 i);
void slider_frame(u16 r);
s16  slider_knob(u16 r);
void be_write(u16 p, s16 count, s16 size, u16 f);
void iff_cmap(u16 f);
void byterun1(u16 src, s16 n, u16 dst);
void iff_body(u16 f);
void cockpit_leave_stub(void);
void gauges_free(void);

/* vcr_panel.c */
u16  hms(u16 out_ds, s32 secs);
void blink_timer(void);
void put_2digits(s16 v, u16 dst);
s32  panel_time(void);
u16  view_name_cpy(u16 s);
u16  view_name_cpy_body(u16 s);
u16  view_name_cat(u16 s);
u16  view_name_cat_body(u16 s);
void dlg_stub(void);

/* vcr_modes.c */
void vcr_panel_force(void);
s16  q_push(u16 q, u16 src);
u16  q_peek(u16 q);
s16  q_full(u16 q);
u16  q_slot(u16 q, u16 i);
u16  q_slot_body(u16 q, u16 i);
void q_dec_wrap(u16 q, u16 off);
void q_dec_wrap_body(u16 q, u16 off);
void panel_stub(u16 ids, u16 bits, s16 set);
void panel_set_bits(u16 ids, u16 bits, s16 set);
s16  panel_item_enabled(s16 id);
void panel_item_dirty(s16 id);
s16  vcr_seek_setup(void);
s16  seek_progress(void);
void seek_draw_bar(s16 n);
s16  vcr_rewind_prompt(void);

/* vcr_runtime.c */
void vcr_debug_log(const char *tag, u32 frame);
void vcr_debug_mouse(u16 ms);
void vcr_panel_show(void);
void vcr_panel_hide(void);
void vcr_step_recorded(s16 no_sim, s16 rerecord);
void vcr_apply_director_request(void);
void vcr_prime(void);
void vcr_mark_edited(void);
s16  vcr_track_views(u16 redraw_ds, u8 flag);
u8   vcr_view_is_edit(u16 v);
s16  vcr_mode_fullscreen(void);
s16  vcr_mode_play(u8 continuous);
s16  vcr_ui(s16 mode);
void vcr_read_simin(void);
void vcr_write_simin(void);
void vcr_gfx_init(void);
u16  vcr_choose_tape(void);
u16  vcr_offer_save(void);
void vcr_exit_gfx(void);
s16  vcr_load(u16 name_ds);
s16  vcr_unload(s16 close);
void demo_fade_once(void);
s16  demo_play(u16 name_ds);
s16  demo_title(void);
void demo_loop(void);

/* stdalert.c */
void alert_btn_cb(void);
void alert_text_cb(void);
void alert_btn_press(void);
void quit_box_draw(void);
s16  end_alert(u16 texts_ds, u16 buttons_ds, u16 codes_ds);
void str_list(u16 out_ds, u16 ids_ds);
s16  object_lost(u16 o);
s16  in_contact(u16 p);
s16  low_and_diving(u16 p);
u16  summary_text(u16 buf, u16 fate);
s16  alert_ids(FarPtr texts, FarPtr buttons);

/* vcr_files.c */
void write_name_rec(u16 f, u16 name);
s16  list_files(u16 pattern, u16 exclude);
s16  reserved_dos_name(u16 s);
void findfile_init(void);
u16  unmarked_name(u16 dst, u16 rec);
u16  tape_version(u16 block, u16 name);
void text_ids(u16 list, s16 group, s16 first, s16 n);
void explain_tape(u16 name);
void modal_depth(s16 d);
s16  tape_commit_copy(void);
u8   tape_menu_load(void);
u16  tape_menu_save(void);
s16  list_other_tapes(u16 pattern);
void tape_menu_delete(void);
void tape_menu_backdrop(void);
s16  list_row_at_mouse(s16 cur, s16 rowh);
u16  list_edit_text(void);
void list_read_selected(void);
void list_read_selected_body(void);
s16  list_set_edit(u16 s);
s16  is_name_char(s16 c);
s16  is_name_char_body(s16 c);
s16  ap_edit_key(u16 scan, u16 ch);
s16  edit_key_body(u16 scan, u16 ch);
void list_edit_poll(u16 key, u16 ascii);
void list_init(s16 n);
void list_reinit(void);
s16  list_open(s16 title1, s16 title2, s16 n);

/* realism.c */
u16  min16(u16 out, u16 a, u16 b);
s16  realism_pct(u16 r);
s16  realism_weight_sum(void);
s16  realism_uncredited(s16 i);
void realism_labels(void);
void realism_label_cb(void);
void realism_label_cb_body(void);
u16  realism_preset_cb(s16 i);
u16  realism_preset_cb_body(s16 i);
s16  realism_cb(s16 v, s16 n);
s16  realism_cb_body(s16 v, s16 n);

/* prefs.c */
s16  calib_center(void);
s16  calib_corners(void);
s16  calib_msg(void);
s16  toggle(s16 a, s16 b, s16 base);
s16  toggle_body(s16 a, s16 b, s16 base);
void calib_slider(s16 a, s16 b);
s16  device_check(s16 item, s16 value);
void device_label(s16 item, s16 base, s16 value);
void device_label_body(s16 item, s16 base, s16 value);
s16  device_select(s16 label, s16 n, s16 item);
void ap_calibrate(s16 first, s16 n, s16 mode, u16 axes);
void devices_dialog(void);
u16  tl_reset(void);
void tl_measure(u16 s, u16 px, u16 py);
void tl_wrap(u16 s, u16 px, u16 py);
FarPtr tl_line_width(FarPtr dst, FarPtr src);
FarPtr tl_align(FarPtr dst, FarPtr lines, s16 n, FarPtr cb);
s16  tl_draw(u16 items, s16 base, FarPtr cb, u16 list, s16 x, s16 y, s16 xmax);
