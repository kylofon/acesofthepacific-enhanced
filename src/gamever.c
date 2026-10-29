/* Game release detection from the program file sizes. See gamever.h. */
#include "gamever.h"

#include <stdio.h>
#include <string.h>

const char *const gamever_exe[GAMEVER_FILES] = { "INTRO.EXE", "PS.EXE", "AP.EXE" };

typedef struct {
    const char *name;
    long size[GAMEVER_FILES];
} Release;

/* The first entry is the one the port runs. The floppy installs (UNCHUNK of RESOURCE.00x) and Patch B
 * (EXES.EXE) were measured on the original disks; Patch B's programs are the floppy 1.2's sizes. The
 * 1946 disk replaces all three programs with its own, whatever version it was installed over. */
static const Release releases[] = {
    { "the CD-ROM version 1.2",                         { 117008, 377136, 494032 } },
    { "the floppy version 1.0",                         {  62493, 167056, 486480 } },
    { "the floppy version 1.01",                        {  62525, 167201, 486768 } },
    { "the floppy version 1.2 (or Patch B)",            {  62239, 169732, 488672 } },
    { "the WWII: 1946 expansion disk",                  {  62780, 175438, 488000 } },
};
#define NRELEASES (sizeof releases / sizeof releases[0])

static const Release *release_of(int file, long size)
{
    for (size_t r = 0; r < NRELEASES; r++)
        if (releases[r].size[file] == size) return &releases[r];
    return NULL;
}

bool gamever_check(const long sizes[GAMEVER_FILES], bool brief, char *out, size_t n)
{
    const Release *rel[GAMEVER_FILES];
    bool same = true;
    for (int i = 0; i < GAMEVER_FILES; i++) {
        rel[i] = sizes[i] < 0 ? NULL : release_of(i, sizes[i]);
        if (rel[i] != rel[0]) same = false;
    }
    if (same && rel[0] == &releases[0]) return true;

    char what[256];
    if (same && rel[0])
        snprintf(what, sizeof what, "These are the files of %s of Aces of the Pacific.", rel[0]->name);
    else if (same)
        snprintf(what, sizeof what, "These are the files of a version of Aces of the Pacific this port doesn't "
                 "know (PS.EXE is %ld bytes, the CD-ROM version's is %ld).", sizes[GAMEVER_PS],
                 releases[0].size[GAMEVER_PS]);
    else {
        size_t o = (size_t)snprintf(what, sizeof what, "The folder mixes files of different versions:");
        for (int i = 0; i < GAMEVER_FILES && o < sizeof what; i++)
            o += (size_t)snprintf(what + o, sizeof what - o, "%s %s is from %s", i ? "," : "", gamever_exe[i],
                                  sizes[i] < 0 ? "nowhere (missing)" : rel[i] ? rel[i]->name : "an unknown version");
        if (o < sizeof what) snprintf(what + o, sizeof what - o, ".");
    }
    if (brief)
        snprintf(out, n, "%s The CD-ROM version 1.2 is needed.", what);
    else
        snprintf(out, n, "%s\n\nThis port runs only the CD-ROM version 1.2, which includes WWII: 1946. The "
                 "floppy releases are separate builds of the game and can't be used: install the game from "
                 "the CD-ROM (to C:\\DYNAMIX\\AOTP) and use that folder.", what);
    return false;
}
