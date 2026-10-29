#pragma once
/* Which release of Aces of the Pacific a game folder holds, from the sizes of INTRO.EXE, PS.EXE and
 * AP.EXE. The port runs only the CD-ROM version 1.2; the floppy releases (1.0, 1.01, 1.2 / Patch B and
 * the WWII: 1946 expansion disk) are separate builds whose code and data sit at other addresses, so
 * they are recognised only to tell the player what they have. Plain C without the host, so the
 * launcher builds it too. */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { GAMEVER_INTRO, GAMEVER_PS, GAMEVER_AP, GAMEVER_FILES };

extern const char *const gamever_exe[GAMEVER_FILES];   /* "INTRO.EXE", "PS.EXE", "AP.EXE" */

/* sizes[i] = size of gamever_exe[i] in bytes, -1 when it is missing. True when all three are the CD
 * version 1.2's. Otherwise writes what the folder holds and what is needed to out: a sentence or two
 * (brief, for the launcher's status line) or a paragraph (for the game's error box). */
bool gamever_check(const long sizes[GAMEVER_FILES], bool brief, char *out, size_t n);

#ifdef __cplusplus
}
#endif
