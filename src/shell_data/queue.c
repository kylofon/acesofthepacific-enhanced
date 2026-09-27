/* shell_data: queue.c (PS 2242) circular linked queue, shell_data_sys.md §9.
 * Queue (6 bytes, near): +0 head sentinel, +2 cur, +4 count. Node (4 bytes): +0 data, +2 next. */
#include "shell_data/sd_priv.h"

/* PS 2242:0005 q_new */
u16 q_new(void)
{
    u16 q = xmalloc(6);
    u16 s = xmalloc(4);
    DSW(q) = s;
    DSW(q + 2) = s;
    DSW(q + 4) = 0;
    DSW(DSW(q + 2)) = 0;
    DSW(DSW(q + 2) + 2) = DSW(q);
    return q;
}

/* PS 2242:003f q_seek_last(q) -> data of the last node (0 when empty) */
static u16 q_seek_last(u16 q)
{
    u16 n = DSW(q);
    for (;;) {
        DSW(q + 2) = DSW(n + 2);
        if (DSW(DSW(q + 2) + 2) == DSW(q)) break;
        n = DSW(q + 2);
    }
    return DSW(DSW(q + 2));
}

/* PS 2242:0067 q_next (uncalled) */
u16 q_next(u16 q)
{
    DSW(q + 2) = DSW(DSW(q + 2) + 2);
    return DSW(DSW(q + 2));
}

/* PS 2242:0081 q_append(q, data) */
s16 q_append(u16 q_ds, s16 data)
{
    if (data == 0) sd_fatal(FS_QUEUE, 0x66);
    q_seek_last(q_ds);
    u16 n = xmalloc(4);
    DSW(n) = (u16)data;
    DSW(n + 2) = DSW(DSW(q_ds + 2) + 2);
    DSW(DSW(q_ds + 2) + 2) = n;
    DSW(q_ds + 2) = n;
    DSW(q_ds + 4)++;
    return data;
}

/* PS 2242:00e6 q_clear (data not freed) */
static void q_clear(u16 q)
{
    for (;;) {
        u16 h = DSW(q), n = DSW(h + 2);
        if (n == DSW(q)) break;
        DSW(h + 2) = DSW(n + 2);
        xfree(n);
        DSW(q + 4)--;
    }
}

/* PS 2242:011e q_delete */
void q_delete(u16 q_ds)
{
    if (!q_ds) sd_fatal(FS_QUEUE, 0x9C);
    q_clear(q_ds);
    if (DSW(q_ds + 4) != 0) sd_fatal(FS_QUEUE, 0x9E);
    xfree(DSW(q_ds));
    xfree(q_ds);
}

/* PS 2242:018b q_rewind */
void q_rewind(u16 q_ds) { DSW(q_ds + 2) = DSW(q_ds); }

/* PS 2242:019a q_at_end */
s8 q_at_end(u16 q_ds) { return DSW(DSW(q_ds + 2) + 2) == DSW(q_ds); }
