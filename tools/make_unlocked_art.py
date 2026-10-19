#!/usr/bin/env python3
"""Creates the main-menu tile of the 'Experimental' single-player mode (art/menu/experimental-bg.webp, label baked in like the
original tiles) in the size and shape of the original SinglePlayer tile.  usage: make_unlocked_art.py <GameData dir>"""
import sys, os
from PIL import Image, ImageDraw, ImageFont

ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'art', 'menu', 'experimental-bg.webp')

TOP = 4                                                                   # rows cut from the top of the scaled picture (at 480 px width): keeps the castle below the label

def make(src, dst):
    tile = Image.open(src).convert('RGBA'); w, h = tile.size; k = w / 512.0
    x0, y0, x1, y1 = tile.getchannel('A').getbbox()                       # the scenery rectangle of the original tile
    bg = Image.open(ART).convert('RGB')
    f = max((x1 - x0) / bg.width, (y1 - y0) / bg.height)                  # cover; the crop keeps the whole castle
    bg = bg.resize((round(bg.width * f), round(bg.height * f)), Image.Resampling.LANCZOS).crop((0, TOP * (x1 - x0) // 480, x1 - x0, TOP * (x1 - x0) // 480 + y1 - y0))
    out = tile.copy(); out.paste(bg, (x0, y0)); out.putalpha(tile.getchannel('A'))
    d = ImageDraw.Draw(out)
    box = [int(124 * k), int(214 * k), int(388 * k), int(297 * k)]        # where the original tiles have their label (centre)
    d.rounded_rectangle(box, radius=int(14 * k), fill=(196, 222, 246, 255), outline=(20, 40, 120, 255), width=max(2, int(3 * k)))
    def font(sz):
        for p in ('/System/Library/Fonts/Supplemental/Comic Sans MS Bold.ttf', '/System/Library/Fonts/Supplemental/Chalkboard.ttc'):
            if os.path.exists(p): return ImageFont.truetype(p, int(sz * k))
        return ImageFont.load_default()
    text, sz = 'EXPERIMENTAL', 27
    fnt = font(sz); tw = d.textlength(text, font=fnt)
    d.text(((box[0] + box[2]) / 2 - tw / 2, (box[1] + box[3]) / 2 - sz * k * 0.62), text, font=fnt, fill=(14, 28, 110, 255))
    out.save(dst)

data = sys.argv[1]
for suffix in ('.png', '@2x.png'):
    s = os.path.join(data, 'SinglePlayer' + suffix)
    if os.path.exists(s): make(s, os.path.join(data, 'SinglePlayerExperimental' + suffix))
