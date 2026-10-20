#!/usr/bin/env python3
"""Align the original part art inside its 128-point sprite canvases.

The selector and gameplay place these same sprites differently. Keep an
unmodified gameplay variant (or a separate gameplay adjustment) alongside
the selector art, preserving every original pixel and its colour layer.
Run after copying the IPA's GameData and before launching the game.
"""
import os
import sys

from PIL import Image


# Offsets are in pixels on the original 256px @2x canvases.
SELECTOR = {
    "BoshHead": (0, -32),
    "CranberryHead": (0, -16),
    "PenguinHead": (0, -48),
    "ReindeerHead": (32, -40),
    "PenguinBody": (0, -16),
    "ReindeerBody": (0, -16),
}
GAME = {
    "BoshHead": (0, 0),
    "CranberryHead": (0, 0),
    "PenguinHead": (0, 24),
    "ReindeerHead": (20, 20),
    "PenguinBody": (0, 0),
    "ReindeerBody": (0, 0),
}


def translated(image, dx, dy):
    output = Image.new("RGBA", image.size, (0, 0, 0, 0))
    output.alpha_composite(image, (max(dx, 0), max(dy, 0)),
                           (max(-dx, 0), max(-dy, 0)))
    return output


def write_variants(data, name, layer, suffix):
    path = os.path.join(data, name + layer + suffix)
    if not os.path.exists(path):
        return
    original = Image.open(path).convert("RGBA")
    scale = original.width / 256
    for target, offset in ((name + "_Game" + layer + suffix, GAME[name]),
                           (name + layer + suffix, SELECTOR[name])):
        dx, dy = (round(v * scale) for v in offset)
        translated(original, dx, dy).save(os.path.join(data, target))


data = sys.argv[1]
for name in SELECTOR:
    for suffix in ("@2x.png", ".png"):
        for layer in ("", "_Color"):
            write_variants(data, name, layer, suffix)
