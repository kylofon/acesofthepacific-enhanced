/* shell_ui: the return from the simulator (PS segment 0542, simend.c) — shell_ui_debrief.md §1. */
#include "shell_ui/sui_a_priv.h"

#define RESULT_FLAGS 0x4229
#define SIM_MODE     0x4B87
#define STATUS       0x4778              /* u8: pilot status (0 active, 1 POW, 2 cook, 3 KIA) */
#define FLY_NOW      0x43D6

/* PS 0542:000a career_status_update */
static void career_status_update(void)
{
    if (DSB(RESULT_FLAGS) & 1) {
        DSB(STATUS) = 3;
    } else if (DSB(RESULT_FLAGS) & 2) {
        DSB(STATUS) = 1;
        DSW(0x477B) = (u16)enddate_get((s8)DSB(0x4779), 1);
        DSW(0x477F) = DSW(0x477B);
    } else if (DSB(0x4794) == 3) {
        DSB(STATUS) = 2;
    }
}

/* PS 0542:004b mission_success(mtype, result) */
static u8 mission_success(s8 t, s16 r)
{
    if (t == 8) {
        if (r > 8) { t = 9; r = (s16)(r - 8); }
    } else if (t > 8) {
        t++;
    }
    u8 sh = (u8)(r - 1);
    return (u8)(DSB((u16)(t + 0x0512)) & (sh >= 8 ? 0 : (u8)(1 << sh)));
}

/* PS 0542:0086 career_date_update(end) -> 1 when the squadron has no further mission */
static u8 career_date_update(u16 end)
{
    if (DSB(RESULT_FLAGS) & 4) squad_sched_init((s16)DSW(0x478E), 0x477B, (u16)(DSW(0x477F) + 90));
    u8 r = squad_sched_next((s16)DSW(0x478E), 0x477B) == 0;
    if (r) DSW(0x477B) = DSW(0x477F);
    if (!(DSB(RESULT_FLAGS) & 4) && end < DSW(0x477F)) {
        DSW(0x477F) = end;
        DSW(0x477B) = end;
    }
    if (DSW(0x4235) == 1 && DSW(0x477F) < end) {
        DSW(0x477F) = end;
        DSW(0x477B) = end;
    }
    DSW(0x4784)++;
    if (mission_success((s8)DSB(0x4B88), DSS(PS_DS_sim_out))) DSW(0x4786)++;
    return r;
}

/* PS 0542:011a career_add_kills */
static void career_add_kills(void)
{
    DSW(0x47BF) += DSW(0x423D);
    DSW(0x47C1) += DSW(0x423B);
    DSW(0x47C3) += DSW(0x4237);
    DSW(0x4792) = (u16)(DSW(0x4792) + DSW(0x423D) + DSW(0x423B));
    DSW(0x47C5) += DSW(0x4245);
    DSW(0x47C7) += DSW(0x4243);
    DSW(0x47C9) += DSW(0x423F);
}

/* PS 0542:0154 career_count_incident */
static void career_count_incident(void)
{
    if (DSB(RESULT_FLAGS) & 0x70) DSB(0x4794)++;
}

/* PS 0542:0168 career_add_score */
static void career_add_score(void)
{
    DSSL(0x47B9) += (s16)DSW(0x4249);
    DSW(0x47BD) += DSW(0x4247);
}

/* PS 0542:0180 career_update(end) -> squad_done */
static u8 career_update(u16 end)
{
    u8 r = 1;
    squad_open();
    career_count_incident();
    career_status_update();
    if (DSB(STATUS) == 0) r = career_date_update(end);
    career_add_kills();
    career_add_score();
    squad_close();
    pilot_store();
    return r;
}

static void show_messages(u16 *m, bool kia_only, u16 name, u16 ext)
{
    ps_strings_load_24e3(make_name(name, -1, ext));
    while (*m && (!kia_only || DSB(*m) == 1)) {
        note_show(*m);
        *m = msg_next();
    }
    strings_free();
}

/* PS 0542:01c4 sim_end: back from AP (messages, debrief, career bookkeeping) */
void sim_end(void)
{
    u8 squad_done = 0;
    DSB(STATUS) = 0;
    if (DSB(SIM_MODE) == 1) DSB(PS_DS_career_dirty) = 1;
    module_set_active(DSB(0x4779));
    sim_in_read();
    if (DSB(SIM_MODE) == 2 || DSB(SIM_MODE) == 3) {
        snd_play(0x3EA);
        return;
    }
    sim_out_read();
    if (DSS(PS_DS_sim_out) == -1) assert_line(0x4991, 0x140);
    if (DSB(SIM_MODE) == 1) {
        u16 src = (u16)((s16)(s8)DSB(PS_DS_sim_in) * 0x7A + 0x0E);
        for (u16 i = 0; i < 0x7A; i++) DSB((u16)(0x4754 + i)) = rd8(SEG(0x4B4C), (u16)(src + i));
        DSW(PS_DS_career_day) = (u16)(DSW(0x477F) + 1);
        campaign_load((s8)DSB(0x4779));
        squad_done = career_update(campaign_end((s16)DSW(0x4790)));
        campaign_close();
    }
    u16 m = msg_next();
    rng_seed_time();
    DSW(PS_DS_career_day_seed) = rng_count();
    if (m && DSB(m) == 1) show_messages(&m, true, 0x051F /* "optional" */, 0x0528 /* "dat" */);
    snd_play(0x3EA);
    DSB(PS_DS_music_changed) = 0;
    debrief_run(1);
    if (m) {
        snd_stop(0x3EA);
        DSB(PS_DS_music_changed) = 1;
        show_messages(&m, false, 0x052C /* "optional" */, 0x0535 /* "dat" */);
    }
    if (DSB(SIM_MODE) != 1) return;
    if (DSB(STATUS) != 0) {                                    /* career over */
        if (DSB(PS_DS_music_changed)) snd_play(0x3EA);
        best_career_insert();
        career_menu_run();
        return;                                                /* (DS:051E is not cleared here) */
    }
    if (!career_after_mission((s8)squad_done)) {
        DSB(PS_DS_career_dirty) = 0;
        if (DSB(PS_DS_music_changed)) {
            snd_stop(-1);
            snd_play(0x3EA);
        }
        career_menu_run();
    } else {
        DSB(PS_DS_career_dirty) = 0;
        module_set_active(DSB(0x4779));
        snd_stop(-1);
        snd_play(0x3F2);
        station_open();
        ord_open();
        squad_open();
        ace_open();
        air_open();
        flight_deck();
        if (DSB(FLY_NOW) == 0) {
            air_close();
            ace_close();
            squad_close();
            ord_close();
            station_close();
        } else {
            squad_close();
            fly_mission(1);
        }
    }
    DSB(PS_DS_career_dirty) = 0;
}
