/* sim_flow: vcr_codec.c (6a63, the VCR frame codec) — sim_flow_vcr.md §4. Records are written to the
 * write stream DS:5016 and read from the read stream DS:502E; the eight Huffman tables are DS:5000..500E
 * (RUD, PAN, VIEW, JOY, TICKS, BTN, SIM, SOUT). The near helpers at xxx / xxx+1 are the Borland
 * "inc bp" entry and its body: one C function, the _body name calls it. */
#include "sim_flow/sfb_priv.h"

#define T_RUD   DSW(0x5000)
#define T_PAN   DSW(0x5002)
#define T_VIEW  DSW(0x5004)
#define T_JOY   DSW(0x5006)
#define T_TICKS DSW(0x5008)
#define T_BTN   DSW(0x500A)
#define T_SIM   DSW(0x500C)
#define T_SOUT  DSW(0x500E)
#define WSTREAM DSW(0x5016)
#define RSTREAM DSW(0x502E)

/* AP 6a63:0000 addon1_flag -> DS:51EC (SIM.IN +7 bit 0) */
s16 addon1_flag(void)
{
    return DSS(0x51EC);
}

/* AP 6a63:0008 huff_get(tab) -> symbol (one bit at a time from the read stream) */
s16 huff_get(u16 tab)
{
    u16 m = stk_mark();
    u16 b = stk_alloc(2);
    DSW(b) = 0;                                       /* keeps its old bit at the end of the data */
    huff_dec_begin(tab);
    do bs_read_bits(b, 1, RSTREAM);
    while (huff_dec_bit(DSS(b)) == 0);
    s16 r = huff_dec_end();
    stk_release(m);
    return r;
}

/* AP 6a63:0046 huff_put(tab, sym) -> ok (0: symbol not in the table, nothing written) */
s16 huff_put(u16 tab, s16 sym)
{
    u16 m = stk_mark();
    u16 nb = stk_alloc(2);
    s16 r = 0;
    u16 code = huff_code(tab, sym, nb);
    if (code != 0 && DSS(nb) > 0) r = bs_write_bits(code, DSS(nb), WSTREAM);
    stk_release(m);
    return r;
}

/* AP 6a63:0084 sext(v, bits) */
u16 sext(u16 v, s16 bits)
{
    u16 b = (u16)(1u << ((bits - 1) & 0x1F));
    if (v & b) v |= (u16)(b * (u16)-2);
    return v;
}

/* Write the low n bits of a near value (bs_write_bits on the stack copy) */
static s16 put_raw(u16 src_ds, s16 n)
{
    return bs_write_bits(src_ds, n, WSTREAM);
}

/* AP 6a63:00a5 view_encode(v) -> ok (near) */
s16 view_encode(u16 v)
{
    u16 m = stk_mark();
    u16 f = stk_alloc(2);
    DSW(f) = 0;
    if (DSW(v + 4) != 0 || DSW(v + 6) != 0) DSW(f) = 1;
    if (DSW(v) != 0) DSW(f) |= 2;
    if (DSW(v + 2) != 0) DSW(f) |= 4;
    u16 r = (u16)huff_put(T_VIEW, DSS(f)) & 1;
    if (DSW(f) & 1) {
        u16 a = (u16)huff_put(T_PAN, DSS(v + 4));
        u16 b = (u16)huff_put(T_PAN, DSS(v + 6));
        r = r & a & b;
    }
    if (DSW(f) & 2) r &= (u16)put_raw(v, (s16)(bitlen16(0x2B) + 1));
    if (DSW(f) & 4) r &= (u16)put_raw((u16)(v + 2), 2);
    stk_release(m);
    return (s16)r;
}
/* AP 6a63:00a6 */
s16 view_encode_body(u16 v) { return view_encode(v); }

/* AP 6a63:0159 view_decode(v) -> 1 (near) */
s16 view_decode(u16 v)
{
    memset(DSP(v), 0, 8);
    s16 f = huff_get(T_VIEW);
    if (f & 1) {
        DSW(v + 4) = (u16)huff_get(T_PAN);
        DSW(v + 6) = (u16)huff_get(T_PAN);
    }
    if (f & 2) bs_read_bits(v, (s16)(bitlen16(0x2B) + 1), RSTREAM);
    if (f & 4) bs_read_bits((u16)(v + 2), 2, RSTREAM);
    return 1;
}
/* AP 6a63:015a */
s16 view_decode_body(u16 v) { return view_decode(v); }

/* AP 6a63:01df throttle_encode(rec) -> ok */
s16 throttle_encode(u16 rec)
{
    u16 m = stk_mark();
    u16 l = stk_alloc(2);                             /* [bp-4] value, [bp-3] absolute flag */
    DSB(l + 1) = DSC(rec + 8) > -1;
    u16 r = (u16)put_raw((u16)(l + 1), 1);
    s16 n;
    if (DSB(l + 1) == 0) {
        DSB(l) = DSB(rec + 9);
        if (DSC(l) > 0) DSB(l) -= 1;
        n = 4;
    } else {
        DSB(l) = DSB(rec + 8);
        n = (s16)(bitlen16(0x40) + 1);
    }
    r &= (u16)put_raw(l, n);
    stk_release(m);
    return (s16)r;
}

/* AP 6a63:0256 ctrl_encode(rec) -> ok (near) */
s16 ctrl_encode(u16 rec)
{
    u16 m = stk_mark();
    u16 f = stk_alloc(2);
    DSW(f) = 0;
    u16 t = DSW(rec + 0x12) ^ DSW(0x5042);
    DSW(0x5042) = DSW(rec + 0x12);
    if (DSW(rec) != 0 || DSW(rec + 2) != 0) DSW(f) = 1;
    if (DSW(rec + 0x10) != 0) DSW(f) |= 2;
    if (DSW(rec + 0x0E) != 0) DSW(f) |= 4;
    if (DSW(rec + 4) != 0) DSW(f) |= 8;
    if (DSC(rec + 8) > -1 || DSB(rec + 9) != 0) DSW(f) |= 0x10;
    u16 r = (u16)huff_put(T_SIM, DSS(f)) & 1;
    if (DSW(f) & 1) {
        u16 a = (u16)huff_put(T_JOY, DSS(rec));
        u16 b = (u16)huff_put(T_JOY, DSS(rec + 2));
        r = r & a & b;
    }
    if (DSW(f) & 2) r &= (u16)put_raw((u16)(rec + 0x10), (s16)(bitlen16(0x7F) + 1));
    if (DSW(f) & 4) r &= (u16)huff_put(T_BTN, DSS(rec + 0x0E));
    r &= (u16)huff_put(T_TICKS, (s16)t);
    if (DSW(f) & 8) r &= (u16)huff_put(T_RUD, DSS(rec + 4));
    if (DSW(f) & 0x10) r &= (u16)throttle_encode(rec);
    stk_release(m);
    return (s16)r;
}
/* AP 6a63:0257 */
s16 ctrl_encode_body(u16 rec) { return ctrl_encode(rec); }

/* AP 6a63:036a ctrl_decode(rec) -> 1 (near) */
s16 ctrl_decode(u16 rec)
{
    memset(DSP(rec), 0, 0x14);
    DSB(rec + 8) = 0xFF;
    s16 f = huff_get(T_SIM);
    if (f & 1) {
        DSW(rec) = (u16)huff_get(T_JOY);
        DSW(rec + 2) = (u16)huff_get(T_JOY);
    }
    if (f & 2) bs_read_bits((u16)(rec + 0x10), (s16)(bitlen16(0x7F) + 1), RSTREAM);
    if (f & 4) DSW(rec + 0x0E) = (u16)huff_get(T_BTN);
    u16 t = (u16)huff_get(T_TICKS) ^ DSW(0x5040);
    DSW(rec + 0x12) = t;
    DSW(0x5040) = t;
    if (f & 8) DSW(rec + 4) = (u16)huff_get(T_RUD);
    if (f & 0x10) {
        u16 m = stk_mark();
        u16 l = stk_alloc(2);                         /* [bp-4] flag, [bp-3] value */
        DSW(l) = 0;
        bs_read_bits(l, 1, RSTREAM);
        if (DSB(l) != 0) {
            bs_read_bits((u16)(l + 1), (s16)(bitlen16(0x40) + 1), RSTREAM);
            DSB(rec + 8) = DSB(l + 1);
        } else {
            bs_read_bits((u16)(l + 1), 4, RSTREAM);
            DSB(rec + 9) = (u8)sext((u16)(s16)DSC(l + 1), 4);
            DSB(rec + 9) = (u8)(DSB(rec + 9) + (DSC(rec + 9) >= 0 ? 1 : 0));
        }
        stk_release(m);
    }
    return 1;
}
/* AP 6a63:036b */
s16 ctrl_decode_body(u16 rec) { return ctrl_decode(rec); }

/* AP 6a63:04a3 put_type(type) -> ok (near) */
s16 put_type(s16 type)
{
    return huff_put(T_SOUT, type);
}
/* AP 6a63:04a4 */
s16 put_type_body(s16 type) { return put_type(type); }

/* AP 6a63:04b8 get_type() -> record type (near) */
s16 get_type(void)
{
    return huff_get(T_SOUT);
}
/* AP 6a63:04b9 */
s16 get_type_body(void) { return get_type(); }

/* AP 6a63:04d0 vcr_put(type, ptr) -> ok */
s16 vcr_put(s16 type, u16 ptr_ds)
{
    if (type == 0 && DSB(AP_DS_vcr_session_mode) != 2) vcr_debug_log("R", DSL(0x0887));
    u16 r = (u16)put_type(type);
    if (r != 0) {
        if (type == 0) r &= (u16)ctrl_encode(ptr_ds);
        else if (type == 1) r &= (u16)view_encode(ptr_ds);
    }
    return (s16)r;
}

/* AP 6a63:0515 vcr_put_data(sub, len, ptr) -> ok */
s16 vcr_put_data(s16 sub, s16 len, u16 ptr_ds)
{
    u16 m = stk_mark();
    u16 a = stk_alloc(4);
    DSW(a) = (u16)sub;
    DSW(a + 2) = (u16)len;
    u16 r1 = (u16)put_type(2);
    u16 r2 = (u16)put_raw(a, 0x10);
    u16 r = 0;
    if (r1 & r2) {
        r = r1 & r2 & (u16)put_raw((u16)(a + 2), 0x10);
        s16 n = len;
        while (r != 0 && n-- > 0) {
            r &= (u16)put_raw(ptr_ds, 8);
            ptr_ds++;
        }
    }
    stk_release(m);
    return (s16)r;
}

/* AP 6a63:0576 vcr_take(dst, len) -> 1 / 0 */
s16 vcr_take(u16 dst_ds, s16 len)
{
    if ((u16)len == DSW(0x5048) && len <= DSS(0x5046)) {
        memmove(DSP(dst_ds), DSP(DSW(0x5044)), (u16)len);
        return 1;
    }
    return 0;
}

/* AP 6a63:05aa vcr_get(&sub, &len) -> record type */
s16 vcr_get(u16 sub_ds, u16 len_ds)
{
    s16 type = get_type();
    switch ((u16)type) {
    case 0:
        DSW(0x5048) = 0x14;
        ctrl_decode(DSW(0x5044));
        break;
    case 1:
        DSW(0x5048) = 8;
        view_decode(DSW(0x5044));
        break;
    case 2:
        bs_read_bits(sub_ds, 0x10, RSTREAM);
        bs_read_bits(len_ds, 0x10, RSTREAM);
        DSW(0x5048) = DSW(len_ds);
        bs_read_bits(DSW(0x5044), (s16)(DSW(len_ds) << 3), RSTREAM);
        break;
    case 4:
        DSW(0x5048) = 0;
        break;
    default:
        break;
    }
    return type;
}
