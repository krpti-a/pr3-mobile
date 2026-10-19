#!/usr/bin/env python3
"""Replaces hat sprites (and their colour layers) with the redrawn art in art/hats, fitted to the original sprite canvases.
usage: import_art.py <GameData dir>   (run after the original data has been copied)

The supplied hats come as a base sprite plus a flat colour layer at another resolution; art/hats/alignment.json (tools/art_align.py)
says where the colour layer sits on the base. Each hat is scaled to fill the bounding box of the sprite it replaces (so it
keeps the original's position on the head), written to <Name>.png / <Name>@2x.png, and its colour layer to <Name>_Color(.png|@2x.png)
as a white silhouette (the game multiplies it by the player's colour and draws the base over it)."""
import sys, os, json
from PIL import Image, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
ART = os.path.join(ROOT, 'art', 'hats')
HATS = {'party': ('PartyHat', 'party-hat'), 'top': ('TopHat', 'top-hat'), 'pirate': ('PirateHat', 'pirate-hat'), 'nurse': ('NurseHat', 'nurse-hat'),
        'happy': ('HappyHat', 'happy-hat'), 'boundy': ('BouncyHat', 'bouncy-hat'), 'sharkfin': ('SharkFin', 'shark-fin-hat'), 'parasol': ('Parasail', 'parasol-hat')}

def whiten(im):
    r, g, b, a = im.split(); w = Image.new('L', im.size, 255)
    return Image.merge('RGBA', (w, w, w, a))

def build(data, key, suffix):
    game, cname = HATS[key]
    old = Image.open(os.path.join(data, game + suffix)).convert('RGBA'); W, H = old.size
    bbox = old.split()[3].getbbox()
    al = json.load(open(os.path.join(ART, 'alignment.json')))[key]
    base = Image.open(os.path.join(ART, key + '.png')).convert('RGBA'); col = Image.open(os.path.join(ART, 'color-layer', cname + '-color.png')).convert('RGBA')
    bb = base.split()[3].getbbox(); base = base.crop(bb)
    ox, oy, ow, oh = bbox[0], bbox[1], bbox[2] - bbox[0], bbox[3] - bbox[1]
    f = min(ow / base.width, oh / base.height)
    nw, nh = max(1, round(base.width * f)), max(1, round(base.height * f))
    x0, y0 = ox + (ow - nw) // 2, oy + (oh - nh) // 2
    out_base = Image.new('RGBA', (W, H), (0, 0, 0, 0)); out_base.alpha_composite(base.resize((nw, nh), Image.Resampling.LANCZOS), (x0, y0))
    # colour layer: position in base-sprite pixels, then the same scale f
    s = al['s']
    px = (al['cx'] - al['bx']) / s - bb[0]; py = (al['cy'] - al['by']) / s - bb[1]
    cw, ch = max(1, round(col.width / s * f)), max(1, round(col.height / s * f))
    c = whiten(col).resize((cw, ch), Image.Resampling.LANCZOS)
    a = c.split()[3].filter(ImageFilter.MinFilter(3)); c.putalpha(a)       # keep it inside the base's outline
    out_col = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    cx, cy = x0 + round(px * f), y0 + round(py * f)
    out_col.alpha_composite(c, (max(cx, 0), max(cy, 0)), (max(-cx, 0), max(-cy, 0))) if True else None
    return out_base, out_col

data = sys.argv[1]
for key, (game, _) in HATS.items():
    for suffix in ('.png', '@2x.png'):
        if not os.path.exists(os.path.join(data, game + suffix)): continue
        b, c = build(data, key, suffix)
        b.save(os.path.join(data, game + suffix))
        c.save(os.path.join(data, game + '_Color' + suffix))
