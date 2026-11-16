#!/usr/bin/env python3
"""Turn the simulator's .ppm screenshots into one labelled PNG contact sheet.

    tools/sim_shots.py sim_out sheet.png
"""
import glob
import os
import sys
from PIL import Image, ImageDraw

src, dst = sys.argv[1], sys.argv[2]
shots = sorted(glob.glob(os.path.join(src, "*.ppm")))
if not shots:
    sys.exit("no screenshots in " + src)
cols = 3
w, h, pad, label = 512, 256, 16, 22
rows = (len(shots) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (w + pad) + pad, rows * (h + pad + label) + pad), (40, 40, 44))
draw = ImageDraw.Draw(sheet)
for i, path in enumerate(shots):
    x = pad + (i % cols) * (w + pad)
    y = pad + (i // cols) * (h + pad + label)
    sheet.paste(Image.open(path), (x, y + label))
    draw.text((x, y + 4), os.path.splitext(os.path.basename(path))[0], fill=(230, 230, 230))
sheet.save(dst)
print(dst, len(shots), "screens")
