#!/usr/bin/env python3
"""Main branch: tidies foot sprites. Every <Name>Foot sprite holds both feet of the character (back foot above, front foot below).
 - Penguin: the original art has only the back foot, so the character walked on one leg: the foot is repeated in the front slot.
 - Turkey, Donkey: the front foot is drawn much farther from the back foot than in every other part: moved closer.
usage: fix_feet.py <GameData dir>   (sizes are for the 256 px @2x canvases; 1x gets half)"""
import sys, os
from PIL import Image

def shifted(im, dx, dy):
    out = Image.new('RGBA', im.size, (0, 0, 0, 0)); out.alpha_composite(im, (max(dx, 0), max(dy, 0)), (max(-dx, 0), max(-dy, 0))); return out

def move_lower(im, cut, dx, dy):
    top = im.copy(); low = im.copy()
    top.paste((0, 0, 0, 0), (0, cut, im.width, im.height)); low.paste((0, 0, 0, 0), (0, 0, im.width, cut))
    top.alpha_composite(shifted(low, dx, dy)); return top

def duplicate_front(im, dx, dy):
    out = im.copy(); out.alpha_composite(shifted(im, dx, dy)); return out

FIXES = {'PenguinFoot': lambda im, k: duplicate_front(im, round(22 * k), round(100 * k)),
         'TurkeyFoot': lambda im, k: move_lower(im, round(130 * k), round(-30 * k), round(-10 * k)),
         'DonkeyFoot': lambda im, k: move_lower(im, round(130 * k), round(-50 * k), round(-17 * k))}

data = sys.argv[1]
for name, fix in FIXES.items():
    for suffix in ('@2x.png', '.png'):
        for part in ('', '_Color'):
            f = os.path.join(data, name + part + suffix)
            if not os.path.exists(f): continue
            im = Image.open(f).convert('RGBA'); fix(im, im.width / 256.0).save(f)
