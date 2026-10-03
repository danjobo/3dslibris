#!/usr/bin/env python3
"""Draws the controls splash shown on the top screen (240x400, portrait).

    python3 scripts/build_splash.py [--preview out.png]

Writes sdmc/3ds/3dslibris/resources/3DSLibris_{light,dark}_small.jpg. The
logo header (book, "3DSlibris", "by Rigle") is kept from the current
images; everything below it is drawn here, so the controls can be
updated when they change. Needs Pillow.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "sdmc", "3ds", "3dslibris", "resources")
FONTS = os.path.join(ROOT, "sdmc", "3ds", "3dslibris", "font")
W, H = 240, 400
HEADER_H = 57  # logo rows kept from the existing image

THEMES = {
    "light": {
        "bg": (251, 235, 212),
        "ink": (58, 40, 22),
        "soft": (128, 100, 70),
        "key_fill": (238, 220, 192),
        "key_ink": (58, 40, 22),
    },
    "dark": {
        "bg": (14, 14, 14),
        "ink": (235, 226, 211),
        "soft": (170, 155, 130),
        "key_fill": (235, 226, 211),
        "key_ink": (23, 14, 0),
    },
}


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


BOLD = font("LiberationSans-Bold.ttf", 13)
REG = font("LiberationSans-Regular.ttf", 12)
SMALL = font("LiberationSans-Regular.ttf", 11)
KEY = font("LiberationSans-Bold.ttf", 11)
TITLE = font("LiberationSans-Bold.ttf", 14)


class Painter:
    def __init__(self, img, theme):
        self.d = ImageDraw.Draw(img)
        self.t = theme

    def text(self, xy, s, f=REG, color="ink", anchor="lm"):
        self.d.text(xy, s, font=f, fill=self.t[color], anchor=anchor)

    def round_key(self, cx, cy, label):
        r = 9
        self.d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=self.t["key_fill"],
                       outline=self.t["ink"], width=1)
        self.d.text((cx, cy), label, font=KEY, fill=self.t["key_ink"],
                    anchor="mm")
        return cx + r

    def shoulder_key(self, x, cy, label, w=20):
        self.d.rounded_rectangle((x, cy - 7, x + w, cy + 7), radius=4,
                                 fill=self.t["key_fill"],
                                 outline=self.t["ink"], width=1)
        self.d.text((x + w / 2, cy), label, font=KEY, fill=self.t["key_ink"],
                    anchor="mm")
        return x + w

    def dpad(self, cx, cy, arrows):
        """A small D-pad; arrows: which arms to mark, e.g. "lr" or "ud"."""
        a, t = 10, 5
        fill, ink = self.t["key_fill"], self.t["ink"]
        self.d.polygon([(cx - t, cy - a), (cx + t, cy - a), (cx + t, cy - t),
                        (cx + a, cy - t), (cx + a, cy + t), (cx + t, cy + t),
                        (cx + t, cy + a), (cx - t, cy + a), (cx - t, cy + t),
                        (cx - a, cy + t), (cx - a, cy - t), (cx - t, cy - t)],
                       fill=fill, outline=ink)
        e = a - 2  # arrow tip, 3 px deep
        tri = {
            "u": [(cx, cy - e), (cx - 2, cy - e + 3), (cx + 2, cy - e + 3)],
            "d": [(cx, cy + e), (cx - 2, cy + e - 3), (cx + 2, cy + e - 3)],
            "l": [(cx - e, cy), (cx - e + 3, cy - 2), (cx - e + 3, cy + 2)],
            "r": [(cx + e, cy), (cx + e - 3, cy - 2), (cx + e - 3, cy + 2)],
        }
        for k in arrows:
            self.d.polygon(tri[k], fill=self.t["key_ink"])
        return cx + a

    def stylus(self, cx, cy):
        ink = self.t["ink"]
        self.d.line([(cx - 6, cy + 6), (cx + 5, cy - 5)], fill=ink, width=3)
        self.d.polygon([(cx - 8, cy + 8), (cx - 7, cy + 4), (cx - 4, cy + 7)],
                       fill=ink)
        return cx + 7

    def section(self, y0, y1, title):
        ink = self.t["ink"]
        self.d.rounded_rectangle((4, y0, W - 5, y1), radius=8, outline=ink,
                                 width=2)
        tw = self.d.textlength(title, font=TITLE)
        self.d.rectangle((W / 2 - tw / 2 - 5, y0 - 2, W / 2 + tw / 2 + 5,
                          y0 + 2), fill=self.t["bg"])
        self.text((W / 2, y0), title, TITLE, anchor="mm")

    def rule(self, y):
        self.d.line([(14, y), (W - 15, y)], fill=self.t["soft"], width=1)


def draw(theme_name):
    t = THEMES[theme_name]
    src = Image.open(os.path.join(RES, "3DSLibris_%s_small.jpg" % theme_name))
    img = Image.new("RGB", (W, H), t["bg"])
    img.paste(src.convert("RGB").crop((0, 0, W, HEADER_H)), (0, 0))
    p = Painter(img, t)

    # Reader.
    p.section(66, 248, "Reader")
    y = 86
    x = p.round_key(20, y, "A") + 3
    x = p.shoulder_key(x, y, "R") + 5
    p.text((x, y), "Next page", BOLD)
    x = p.round_key(132, y, "B") + 3
    x = p.shoulder_key(x, y, "L") + 5
    p.text((x, y), "Previous", BOLD)
    p.text((12, y + 16), "also the D-pad, or tap the right / left half",
           SMALL, "soft")
    p.rule(y + 25)

    y = 125
    x = p.dpad(22, y, "lr") + 6
    p.text((x, y), "Jump between bookmarks", BOLD)
    p.rule(y + 15)

    y = 154
    x = p.round_key(20, y, "Y") + 6
    p.text((x, y - 6), "Bookmark this page", BOLD)
    p.text((x, y + 7), "hold: look up a word", REG)
    p.rule(y + 18)

    y = 186
    x = p.round_key(20, y, "X") + 6
    p.text((x, y - 6), "Change colors", BOLD)
    p.text((x, y + 7), "hold: highlight, notes, characters", REG)
    p.rule(y + 18)

    y = 219
    x = p.stylus(20, y) + 6
    p.text((x, y - 6), "Tap a link to follow it", BOLD)
    p.text((x, y + 7), "hold Y/X modes: touch words", REG)

    # PDF / CBZ.
    p.section(262, 334, "PDF / CBZ")
    y = 281
    x = p.shoulder_key(14, y, "R") + 3
    x = p.dpad(x + 11, y, "r") + 5
    p.text((x, y), "Next", BOLD)
    x = p.shoulder_key(122, y, "L") + 3
    x = p.dpad(x + 11, y, "l") + 5
    p.text((x, y), "Previous", BOLD)
    y = 303
    x = p.round_key(20, y, "A") + 4
    p.text((x, y), "Zoom in", REG)
    x = p.round_key(132, y, "B") + 4
    p.text((x, y), "Zoom out", REG)
    y = 320
    x = p.dpad(22, y, "ud") + 6
    p.text((x, y), "Next / previous chapter", REG)

    # Footer.
    y = 352
    x = p.shoulder_key(6, y, "SELECT", 46) + 5
    p.text((x, y - 6), "Settings", BOLD)
    p.text((x, y + 7), "twice: characters", SMALL)
    x = p.shoulder_key(146, y, "START", 40) + 5
    p.text((x, y - 6), "Library", BOLD)
    p.text((W / 2, 384), "Settings and progress are saved automatically.",
           SMALL, "soft", anchor="mm")
    return img


def main():
    preview = None
    if len(sys.argv) == 3 and sys.argv[1] == "--preview":
        preview = sys.argv[2]
    elif len(sys.argv) != 1:
        sys.exit(__doc__)
    images = {name: draw(name) for name in THEMES}
    if preview:
        sheet = Image.new("RGB", (W * 2 + 8, H), (128, 128, 128))
        sheet.paste(images["light"], (0, 0))
        sheet.paste(images["dark"], (W + 8, 0))
        sheet.save(preview)
        return
    for name, img in images.items():
        img.save(os.path.join(RES, "3DSLibris_%s_small.jpg" % name),
                 quality=95)


if __name__ == "__main__":
    main()
