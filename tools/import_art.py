#!/usr/bin/env python3
"""Replaces hat sprites (and their colour layers) with the redrawn art in art/hats, fitted to the original sprite canvases.
usage: import_art.py <GameData dir>   (run after the original data has been copied)

The supplied hats come as a base sprite plus a flat colour layer at another resolution; art/hats/alignment.json (tools/art_align.py)
says where the colour layer sits on the base. Each hat is scaled to fill the bounding box of the sprite it replaces (so it
keeps the original's position on the head), written to <Name>.png / <Name>@2x.png, and its colour layer to <Name>_Color(.png|@2x.png)
as a white silhouette (the game multiplies it by the player's colour and draws the base over it)."""
import sys, os, json
from PIL import Image, ImageFilter
import numpy as np
from scipy import ndimage as ndi

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
ART = os.path.join(ROOT, 'art', 'hats')
HATS = {'party': ('PartyHat', 'party-hat'), 'top': ('TopHat', 'top-hat'), 'pirate': ('PirateHat', 'pirate-hat'), 'nurse': ('NurseHat', 'nurse-hat'),
        'happy': ('HappyHat', 'happy-hat'), 'boundy': ('BouncyHat', 'bouncy-hat'), 'sharkfin': ('SharkFin', 'shark-fin-hat'), 'parasol': ('Parasail', 'parasol-hat')}

def whiten(im):
    r, g, b, a = im.split(); w = Image.new('L', im.size, 255)
    return Image.merge('RGBA', (w, w, w, a))

# Hats whose supplied colour layer has the right shape but sits a little off the coloured area of the sprite (the translucent
# interior, which the game tints from underneath): its position and scale are fitted to that area. The fit is made on the @2x
# sprite and reused (as fractions of the canvas) for the 1x one.
FIT = set()                                                             # (fitting the layer to the sprite was worse than using the artist's own placement)
FITS = {}
CLIP = {'boundy', 'nurse'}
FLAT = set()                                       # (cutting the hat out under the layer lost its shading: the hat's own translucent shading must stay on top)

def coloured_area(base, guide):
    """Translucent interior of the sprite near the supplied layer: where the tinted layer shows through."""
    a = np.array(base.split()[3]).astype(int); g = np.array(guide.split()[3]) > 128
    r = max(1, round(3 * base.width / 256))
    dist = ndi.distance_transform_edt(a > 0)
    core = ndi.binary_opening((a > 0) & (a < 250) & (dist > r), np.ones((3, 3)))
    lab, n = ndi.label(core); near = ndi.binary_dilation(g, iterations=6 * r); keep = np.zeros_like(core)
    for i in range(1, n + 1):
        comp = lab == i
        if comp.sum() >= 12 * r * r and (comp & near).sum() >= 0.2 * comp.sum(): keep |= comp
    return keep, a

def fit_color(out_base, out_col, key):
    W = out_base.width
    if key in FITS:                                                     # 1x: reuse the @2x fit
        fx, fy, sc = FITS[key]; dx, dy = round(fx * W), round(fy * W)
    else:
        area, a = coloured_area(out_base, out_col)
        g0 = out_col.split()[3]
        if not area.any(): return out_col
        outside = a == 0
        gm = np.array(g0) > 128; cx, cy = ndi.center_of_mass(gm)
        best = (-9, 0, 0, 1.0)
        for sc in np.arange(0.90, 1.101, 0.02):
            m = Image.new('L', g0.size, 0)
            scaled = g0.transform(g0.size, Image.AFFINE, (1 / sc, 0, cx - cx / sc, 0, 1 / sc, cy - cy / sc), resample=Image.BILINEAR)
            sm = np.array(scaled) > 128
            for dy in range(-18, 19):
                for dx in range(-18, 19):
                    t = np.roll(np.roll(sm, dy, axis=0), dx, axis=1)
                    score = (t & area).sum() / area.sum() - 0.6 * (t & outside).sum() / max(1, t.sum())
                    if score > best[0]: best = (score, dx, dy, sc)
        _, dx, dy, sc = best
        FITS[key] = (dx / W, dy / W, sc)
        print('  fit', key, 'dx', dx, 'dy', dy, 'scale %.2f' % sc, 'score %.3f' % best[0])
    gm = np.array(out_col.split()[3]) > 0; cx, cy = ndi.center_of_mass(gm)
    ch = out_col.split()[3].transform(out_col.size, Image.AFFINE, (1 / sc, 0, cx - cx / sc - dx, 0, 1 / sc, cy - cy / sc - dy), resample=Image.BILINEAR)
    k = max(1, round(W / 256))
    solid = ndi.binary_fill_holes(ndi.binary_closing(np.array(out_base.split()[3]) > 8, structure=np.ones((3, 3)), iterations=3 * k))
    inside = ndi.binary_erosion(solid, iterations=k)                                                            # no colour outside the sprite's outline
    ch = Image.fromarray(np.where(inside, np.array(ch), 0).astype(np.uint8))
    if key == 'top':                                   # the band is a see-through stripe of the base: use exactly that stripe
        a = np.array(out_base.split()[3]); near = ndi.binary_dilation(np.array(ch) > 128, iterations=3 * k)
        stripe = solid & (a < 250) & near & ndi.binary_erosion(solid, iterations=2 * k)
        stripe = ndi.binary_closing(stripe, structure=np.ones((3, 3)), iterations=k)
        ch = Image.fromarray((stripe * 255).astype(np.uint8))
    w = Image.new('L', out_col.size, 255)
    return Image.merge('RGBA', (w, w, w, ch))

# After the fit, hats that sit awkwardly on the player's head are moved (pixels of the @2x canvas; the 1x canvas gets half).
SHIFT = {'TopHat': (15, 0), 'PartyHat': (10, 0), 'PirateHat': (22, -3), 'NurseHat': (13, 0), 'HappyHat': (17, 0), 'SharkFin': (20, 0),
         'Parasail': (8, 0), 'BaseballHat': (21, -6)}

def shift_image(im, dx, dy):
    out = Image.new('RGBA', im.size, (0, 0, 0, 0)); out.alpha_composite(im, (max(dx, 0), max(dy, 0)), (max(-dx, 0), max(-dy, 0)))
    return out

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
    out_col = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    cx, cy = x0 + round(px * f), y0 + round(py * f)
    out_col.alpha_composite(c, (max(cx, 0), max(cy, 0)), (max(-cx, 0), max(-cy, 0))) if True else None
    if key in FIT: out_col = fit_color(out_base, out_col, key)
    if key in CLIP:                                    # colour only where the hat's own silhouette is (the layer is a little larger than the hat)
        k = max(1, round(W / 256))
        solid = ndi.binary_fill_holes(ndi.binary_closing(np.array(out_base.split()[3]) > 8, structure=np.ones((3, 3)), iterations=3 * k))
        inside = ndi.binary_erosion(solid, iterations=k)
        out_col.putalpha(Image.fromarray(np.where(inside, np.array(out_col.split()[3]), 0).astype(np.uint8)))
    if key in FLAT:
        # The artist's preview draws the flat colour layer ON TOP of the hat (see art/hats/color-layer/preview); the game draws it
        # underneath, so the hat is cut out where the layer is.
        a = np.array(out_base.split()[3]).astype(float) * (1 - np.array(out_col.split()[3]).astype(float) / 255.0)
        out_base.putalpha(Image.fromarray(a.astype(np.uint8)))
    return out_base, out_col

data = sys.argv[1]
for key, (game, _) in HATS.items():
    for suffix in ('@2x.png', '.png'):                                   # @2x first: the colour-layer fit is made there
        if not os.path.exists(os.path.join(data, game + suffix)): continue
        b, c = build(data, key, suffix)
        b.save(os.path.join(data, game + suffix))
        c.save(os.path.join(data, game + '_Color' + suffix))
for name, (dx, dy) in SHIFT.items():
    for suffix in ('@2x.png', '.png'):
        for part in ('', '_Color'):
            f = os.path.join(data, name + part + suffix)
            if not os.path.exists(f): continue
            im = Image.open(f).convert('RGBA'); k = im.width / 256.0
            shift_image(im, round(dx * k), round(dy * k)).save(f)
