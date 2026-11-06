#!/usr/bin/env python3
# Usage: scripts/check_partialstream.py <JuceMetalCoverage --partialstream output>/*_metal.png
# The --partialstream scene repaints one of 16 cells per frame, its paint number encoded in its
# colour: any capture must show 16 consecutive numbers, each in its cell. A cell drawn from stale
# content (a lost or out-of-date partial repaint) shows an older number.
import sys
from PIL import Image
def lvl(c): return max(0,min(7,round((c-16)/32)))
for f in sys.argv[1:]:
    im=Image.open(f).convert('RGB'); W,H=im.size; s=W/320; top=H-round(320*s); px=im.load()
    vals=[]
    for i in range(16):
        x=round((20+(i%4)*70+30)*s); y=top+round((20+(i//4)*70+30)*s)
        r,g,b=px[x,y]; vals.append(lvl(r)+8*lvl(g)+64*lvl(b))
    ok_mod=all((v-i)%16==0 for i,v in enumerate(vals))
    # the newest value: cells hold n-15..n (mod 512)
    newest=max(range(16), key=lambda i: [ (vals[i]-vals[j])%512 < 256 for j in range(16)].count(True))
    n=vals[newest]
    ok=ok_mod and all((n-vals[i])%512 < 16 for i in range(16))
    print(f.split('/')[-1], 'OK' if ok else 'BAD', vals if not ok else '')
