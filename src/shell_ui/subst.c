/* shell_ui: career text substitutions (PS segment 0cfe, not indexed by the spec's tools) —
 * shell_ui_debrief.md §8. display_subst is the flow_text %c callback of the debrief, the award
 * ceremonies and the text boxes. */
#include "shell_ui/sui_a_priv.h"

u16 medal_name(s8 m);                    /* shell_data/aircraft.c (PS 27f0:00f5) */
u16 rank_name(s8 r);                     /* shell_data/ranks.c (PS 225d:00f7) */

#define SUBST_BUF 0x43FA

/* PS 0cfe:000a subst_ace_name: the enemy ace's name (no NUL copied: a shorter name keeps the tail) */
static FarPtr subst_ace_name(void)
{
    ace_open();
    FarPtr s = ace_name((s16)DSC(0x4B89));
    u8 n = (u8)rtl__fstrlen(s);
    fmemcpy(ds_ptr(SUBST_BUF), s, (u16)(s16)(s8)n);
    ace_close();
    return ds_ptr(SUBST_BUF);
}

/* PS 0cfe:0057 display_subst(c) -> far text */
FarPtr display_subst(s8 c)
{
    s16 id;
    switch (c) {
    case 'a': return ds_ptr(0x4754);                                   /* pilot name */
    case 'b': date_text(DSW(0x477F), SUBST_BUF); return ds_ptr(SUBST_BUF);
    case 'e': id = DSC(PS_DS_career_side); break;
    case 'f': id = (DSB(0x4229) & 0x10) ? 6 : (DSB(0x4229) & 0x20) ? 7 : 8; break;
    case 'g': id = (s16)(DSC(0x4776) + 9); break;
    case 'h': return ds_ptr(medal_name((s8)DSB(PS_DS_medal_awarded)));
    case 'i': return ds_ptr(rank_name((s8)DSB(PS_DS_promotion_rank)));
    case 'j': return subst_ace_name();
    case 'p': return squad_name((s16)DSW(0x478E));
    case 'q': return ds_ptr(campaign_name((s16)DSW(0x4790)).off);
    case 'u': return ac_name((s16)DSW(0x478A));
    case 'v': id = (s16)(DSC(PS_DS_career_side) + 20); break;
    case 'w':
        stattxt_open();
        strcpy_far_src(SUBST_BUF, station_name((s16)DSW(0x478C)));
        stattxt_close();
        return ds_ptr(SUBST_BUF);
    case 'x': id = (s16)(DSC(0x4B89) + 26); break;
    default: return ds_ptr(0x0956);                                    /* "" */
    }
    return strings_get(0, id);
}

__attribute__((constructor)) static void subst_codeptrs(void)
{
    REG(display_subst);
}
