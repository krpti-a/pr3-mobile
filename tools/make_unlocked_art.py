#!/usr/bin/env python3
"""Creates the main-menu tile for the 'all unlocked' single-player mode from the original SinglePlayer art:
the scenery is hue-shifted and the baked-in "SINGLE PLAYER" label is replaced.  usage: make_unlocked_art.py <GameData dir>"""
import sys, os, colorsys
from PIL import Image, ImageDraw, ImageFont, ImageColor

def make(src, dst):
    im = Image.open(src).convert('RGBA'); w, h = im.size; k = w / 512.0
    # hue shift (autumn -> teal) on the opaque pixels
    px = im.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0: continue
            hh, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            r2, g2, b2 = colorsys.hsv_to_rgb((hh + 0.5) % 1.0, s, v)
            px[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255), a)
    d = ImageDraw.Draw(im)
    box = [int(112 * k), int(204 * k), int(400 * k), int(306 * k)]
    d.rounded_rectangle(box, radius=int(14 * k), fill=(196, 222, 246, 255), outline=(20, 40, 120, 255), width=max(2, int(3 * k)))
    def font(sz):
        for p in ('/System/Library/Fonts/Supplemental/Comic Sans MS Bold.ttf', '/System/Library/Fonts/Supplemental/Chalkboard.ttc'):
            if os.path.exists(p): return ImageFont.truetype(p, int(sz * k))
        return ImageFont.load_default()
    for text, sz, dy in (('ALL UNLOCKED', 29, 0),):
        f = font(sz); tw = d.textlength(text, font=f)
        d.text(((box[0] + box[2]) / 2 - tw / 2, (box[1] + box[3]) / 2 - sz * k * 0.62 + dy * k), text, font=f, fill=(14, 28, 110, 255))
    im.save(dst)

data = sys.argv[1]
for suffix in ('.png', '@2x.png'):
    s = os.path.join(data, 'SinglePlayer' + suffix)
    if os.path.exists(s): make(s, os.path.join(data, 'SinglePlayerAll' + suffix))
