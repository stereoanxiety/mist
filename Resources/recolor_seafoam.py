#!/usr/bin/env python3
"""Recolor Mist raster assets teal -> vapor seafoam to match the C++ palette
(MistLookAndFeel teal = 0xff5cb0a6, hue ~173deg). Hue-only HSV remap:
preserves brushed-metal shading (Value) and saturation, moves only color.

The whole cool band (greens..cyans, COOL_LO..COOL_HI deg) is compressed onto a
narrow seafoam band (DEST_LO..DEST_HI, centre ~173) so greens and teals both
converge to seafoam while relative shading variation is kept. Warm outliers
fall back to a simple rotation. Originals backed up to .orig_teal/.

Run AFTER seeding Resources/ with the pristine teal masters from dust/Resources/
(not Haze's already-periwinkle PNGs)."""
import os, shutil
from PIL import Image

COOL_LO, COOL_HI = 60, 220          # source hue band (deg): green -> cyan
DEST_LO, DEST_HI = 162, 184         # target seafoam band (deg), centre ~173
ROT_DEG = 50                        # fallback rotation for warm outliers
HERE = os.path.dirname(os.path.abspath(__file__))
BACKUP = os.path.join(HERE, ".orig_teal")
FILES = ["background.png", "led_teal.png", "led_chrome.png",
         "button_on.png", "button_off.png", "knob.png"]   # NOT logo.png

def hue_lut(x):                     # x: PIL hue 0..255
    deg = x * 360.0 / 256.0
    if COOL_LO <= deg <= COOL_HI:
        nd = DEST_LO + (deg - COOL_LO) * (DEST_HI - DEST_LO) / (COOL_HI - COOL_LO)
    else:
        nd = deg + ROT_DEG
    return round(nd * 256.0 / 360.0) % 256

os.makedirs(BACKUP, exist_ok=True)

for name in FILES:
    p = os.path.join(HERE, name)
    bak = os.path.join(BACKUP, name)
    if not os.path.exists(bak):
        shutil.copy2(p, bak)        # one-time backup of pristine teal asset
    src = bak                       # always recolor from pristine, idempotent
    im = Image.open(src).convert("RGBA")
    a = im.getchannel("A")
    h, s, v = im.convert("RGB").convert("HSV").split()
    h2 = h.point(hue_lut)
    out = Image.merge("HSV", (h2, s, v)).convert("RGB")
    out.putalpha(a)
    out.save(p)
    print(f"recolored {name}")

print(f"done. backups in {BACKUP}")
