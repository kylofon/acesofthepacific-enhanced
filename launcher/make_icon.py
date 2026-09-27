"""Writes app.ico: the fighter over the sea drawn in icon.cpp, for Explorer. Needs Pillow.

    python make_icon.py
"""
from pathlib import Path

from PIL import Image

PLANE = [
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
]
COLOURS = {"#": 0x5E6B3A, "o": 0x202830, "y": 0xFFD040}

tile = Image.new("RGBA", (16, 16))
for y in range(16):
    for x in range(16):
        c = 0x6FA8E0 if y < 9 else 0xB8D8F0 if y == 9 else 0xE8F0F8 if y == 12 and x % 4 < 2 else 0x1C4A8C
        c = COLOURS.get(PLANE[y][x], c)
        tile.putpixel((x, y), (c >> 16, (c >> 8) & 0xFF, c & 0xFF, 255))

sizes = [16, 20, 24, 32, 48, 64, 256]
big = tile.resize((256, 256), Image.NEAREST)
big.save(Path(__file__).with_name("app.ico"), sizes=[(s, s) for s in sizes])
