#!/usr/bin/env python3
# Usage: scripts/compare_runs.py <coverage output A> <coverage output B> [engine=metal]
# Pixel differences between two JuceMetalCoverage runs (same flags), per scenario: e.g. before and
# after a renderer change. Runs with different --rows show different frames (the frozen clock moves
# per scenario), so compare like with like.
import sys, os, glob
from PIL import Image, ImageChops
a, b = sys.argv[1], sys.argv[2]
engine = sys.argv[3] if len(sys.argv) > 3 else 'metal'
for fa in sorted(glob.glob(os.path.join(a, f'*_{engine}.png'))):
    fb = os.path.join(b, os.path.basename(fa))
    if not os.path.exists(fb) or os.path.getsize(fa) == 0 or os.path.getsize(fb) == 0:
        print(f'{os.path.basename(fa):40s} missing'); continue
    x, y = Image.open(fa).convert('RGBA'), Image.open(fb).convert('RGBA')
    if x.size != y.size:
        print(f'{os.path.basename(fa):40s} different sizes'); continue
    d = [max(p) for p in ImageChops.difference(x, y).getdata()]
    over = sum(v > 16 for v in d)
    print(f'{os.path.basename(fa):40s} ' + ('identical' if max(d) == 0 else f'{sum(v > 0 for v in d)} px differ, {over} by more than 16 levels, max {max(d)}'))
