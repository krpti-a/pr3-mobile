#!/usr/bin/env python3
"""Repairs data defects of the original IPA. usage: fix_data.py <GameData dir>

* PropellerHat_Color(.png|@2x.png) is an empty placeholder in the shipped app (the same 475-byte transparent image as the
  hats that are not tintable), so the beanie's grey wedge can never take the player's hat colour. The colour layer is rebuilt
  from the grey wedge of the base sprite: the game multiplies it by the chosen colour and draws it over the base."""
import sys, os, colorsys
from PIL import Image

def propeller_color(base_path, out_path):
    """Other hats ship a flat white silhouette of their colourable area; the base sprite (shading, outlines, fixed colours) is
    drawn over it.  For the beanie the colourable area is the dome except the yellow wedge, the red blades and the stem."""
    base = Image.open(base_path).convert('RGBA'); w, h = base.size; k = w / 256.0
    bp = base.load()
    def kind(x, y):
        r, g, b, a = bp[x, y]
        if a < 8: return 'empty'
        hh, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        if a > 150 and s > 0.55 and v > 0.3 and (hh < 0.04 or hh > 0.94): return 'red'
        if a > 150 and s > 0.55 and v > 0.3 and 0.45 < hh < 0.75: return 'blue'
        if a > 150 and s > 0.55 and v > 0.3 and 0.07 < hh < 0.20: return 'yellow'
        return 'other'
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0)); op = out.load()
    yel = Image.new('L', (w, h), 0); yp = yel.load()
    for y in range(int(70 * k), int(152 * k)):
        kinds = [kind(x, y) for x in range(w)]
        xs = [x for x in range(w) if kinds[x] == 'other']
        if not xs: continue
        lo, hi = min(xs), max(xs)
        for x in range(lo, hi + 1):
            if kinds[x] == 'yellow': yp[x, y] = 255
            if kinds[x] in ('yellow', 'red', 'blue'): continue
            op[x, y] = (255, 255, 255, 255)
    # pull the colour layer in from the outline so it does not show as a fringe around the sprite's dark edge
    from PIL import ImageFilter
    orig = out.split()[3]
    a = orig.filter(ImageFilter.MinFilter(2 * int(round(k)) + 1))
    near_yellow = yel.filter(ImageFilter.MaxFilter(4 * int(round(k)) + 1))    # keep the seam with the yellow wedge closed
    from PIL import ImageChops
    a = ImageChops.lighter(a, ImageChops.multiply(orig, near_yellow))
    # and keep it well inside the sprite's own silhouette (the sides of the dome showed the colour past the outline)
    import numpy as np
    from scipy import ndimage as ndi
    kk = int(round(k))
    solid = ndi.binary_fill_holes(ndi.binary_closing(np.array(base.split()[3]) > 8, structure=np.ones((3, 3)), iterations=3 * kk))
    inside = ndi.binary_erosion(solid, iterations=3 * kk)
    a = Image.fromarray(np.where(inside, np.array(a), 0).astype(np.uint8))
    out.putalpha(a)
    out.save(out_path)

data = sys.argv[1]
for suffix in ('.png', '@2x.png'):
    b = os.path.join(data, 'PropellerHat' + suffix)
    if os.path.exists(b): propeller_color(b, os.path.join(data, 'PropellerHat_Color' + suffix))
