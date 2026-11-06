#!/usr/bin/env python3
# Usage: scripts/check_imagestream.py <JuceMetalCoverage --imagestream output>/*.png
# The --imagestream scene rewrites persistent images every frame and draws each one next to the same
# picture drawn as vectors: the two columns must match (stale or overwritten image textures show).
import sys
from PIL import Image
for f in sys.argv[1:]:
    im = Image.open(f).convert('RGB'); W, H = im.size
    s = W / 420; top = H - round(330 * s); px = im.load()
    out = []
    for y0 in (20, 120, 220):
        bad = maxd = 0; n = 0
        for yy in range(round(y0*s), round((y0+64)*s)):
            for xx in range(round(20*s), round(116*s)):
                a = px[xx, top+yy]; b = px[xx + round(120*s), top+yy]
                d = max(abs(a[i]-b[i]) for i in range(3)); n += 1
                maxd = max(maxd, d); bad += d > 4
        out.append(f"row{y0}: {100*bad/n:.2f}% >4 (max {maxd})")
    print(f.split('/')[-1], ' | '.join(out))
