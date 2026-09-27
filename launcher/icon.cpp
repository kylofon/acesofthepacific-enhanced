// icon.cpp -- the app icon: a fighter seen from above, climbing over the Pacific (a light sky over
// dark blue sea with a white wave line), drawn at 16x16 and scaled by whole pixels so every size shows
// the same picture. app.ico holds the same drawing for Explorer; make_icon.py writes it.
#include "icon.h"

#include <wx/image.h>

namespace {

// The plane: '#' fuselage and wings, 'o' the canopy, 'y' the propeller disc.
const char* const PLANE[16] = {
    "................",
    ".......yy.......",
    ".......##.......",
    ".......##.......",
    ".......o#.......",
    "..############..",
    ".##############.",
    "..############..",
    ".......##.......",
    ".......##.......",
    ".......##.......",
    ".....######.....",
    "................",
    "................",
    "................",
    "................",
};

wxImage PlaneTile() {
    wxImage tile(16, 16);
    auto put = [&](int x, int y, unsigned long rgb) { tile.SetRGB(x, y, rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF); };
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            unsigned long c = y < 9 ? 0x6FA8E0 : y == 9 ? 0xB8D8F0 : y == 12 && x % 4 < 2 ? 0xE8F0F8 : 0x1C4A8C;
            switch (PLANE[y][x]) {
            case '#': c = 0x5E6B3A; break;
            case 'o': c = 0x202830; break;
            case 'y': c = 0xFFD040; break;
            }
            put(x, y, c);
        }
    return tile;
}

}  // namespace

wxBitmap AppBitmap(int size) { return wxBitmap(PlaneTile().Scale(size, size, wxIMAGE_QUALITY_NEAREST)); }

wxIconBundle AppIcons() {
    wxIconBundle icons;
    for (int size : {16, 20, 24, 32, 48, 64, 256}) {
        wxIcon icon;
        icon.CopyFromBitmap(AppBitmap(size));
        icons.AddIcon(icon);
    }
    return icons;
}
