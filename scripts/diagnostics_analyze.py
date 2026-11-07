#!/usr/bin/env python3
# Usage: scripts/diagnostics_analyze.py <output directory of diagnostics_sweep.sh>
# Per example and engine: the demo's "total (median)" paint time, and CPU / GPU milliseconds per
# second of DemoRunner and WindowServer (GPU: the union of each process's GPU intervals in the
# engine's trace); then the median and mean over the examples per engine.
import sys, subprocess, re, datetime, collections, statistics as st
import xml.etree.ElementTree as ET
import os
S = os.path.join(sys.argv[1], '')

def rows(path):
    refs = {}
    for ev, el in ET.iterparse(path, events=('end',)):
        if el.tag == 'row':
            vals = []
            for c in el:
                if 'ref' in c.attrib:
                    c = refs.get(c.get('ref'), c)
                else:
                    for sub in c.iter():
                        if 'id' in sub.attrib:
                            refs[sub.get('id')] = sub
                vals.append(c)
            yield vals
            el.clear()

def union(iv):
    iv.sort(); tot = 0; cs = ce = None
    for s, e in iv:
        if cs is None: cs, ce = s, e
        elif s <= ce: ce = max(ce, e)
        else: tot += ce - cs; cs, ce = s, e
    if cs is not None: tot += ce - cs
    return tot

windows = collections.defaultdict(list)
for l in open(S + 'windows.txt'):
    p = l.rstrip('\n').split('|')
    if len(p) >= 9:
        windows[p[0]].append(p)

results = []  # (engine, item, demo ms, app CPU, app GPU, WS CPU, WS GPU) per second of wall time
for i, ws in windows.items():
    trace = S + f'engine{i}.trace'; xml = S + f'engine{i}_gpu.xml'
    toc = subprocess.run(['xcrun', 'xctrace', 'export', '--input', trace, '--toc'], capture_output=True, text=True).stdout
    m = re.search(r'<start-date>([^<]+)</start-date>', toc)
    start = datetime.datetime.fromisoformat(m.group(1)).timestamp() if m else None
    if not os.path.exists(xml):
        with open(xml, 'w') as f:
            subprocess.run(['xcrun', 'xctrace', 'export', '--input', trace, '--xpath',
                            '/trace-toc/run[@number="1"]/data/table[@schema="metal-gpu-intervals"]'],
                           stdout=f, stderr=subprocess.DEVNULL)
    iv = {'app': [], 'ws': []}
    if start:
        for c in rows(xml):
            f = [(x.get('fmt') or '') for x in c]
            proc = f[10] if len(f) > 10 else ''
            k = 'app' if 'DemoRunner' in proc else ('ws' if 'WindowServer' in proc else None)
            if k is None or c[0].text is None: continue
            s = int(c[0].text) / 1e9 + start
            iv[k].append((s, s + int(c[1].text) / 1e9))
    for p in ws:
        _, selected, item, t0, t1, ca, cw, timings = p[:8]
        t0 = float(t0); t1 = float(t1); dur = t1 - t0
        v = [float(x) for x in timings.split(',') if x]
        demo = v[1] if len(v) >= 2 else float('nan')  # the legend's second line: total (median)
        busy = lambda k: union([(max(s, t0), min(e, t1)) for s, e in iv[k] if e > t0 and s < t1]) * 1000 / dur
        results.append((selected, item, demo, float(ca) / dur, busy('app'), float(cw) / dur, busy('ws')))

engines = []
for r in results:
    if r[0] not in engines: engines.append(r[0])
items = []
for r in results:
    if r[1] not in items: items.append(r[1])
get = {(r[0], r[1]): r for r in results}

print('Per example: demo total (median) ms | app CPU, app GPU | WindowServer CPU, GPU (ms per second)')
print('%-44s' % 'example' + ''.join(' | %-30s' % e[:30] for e in engines))
for it in items:
    cells = []
    for e in engines:
        r = get.get((e, it))
        cells.append('%7.3f %4.0f %4.0f | %4.0f %3.0f' % r[2:] if r else '-')
    print('%-44s' % it[:44] + ''.join(' | %-30s' % c for c in cells))
print()
for e in engines:
    rs = [r for r in results if r[0] == e]
    for name, f in (('median', st.median), ('mean', st.mean)):
        vals = [f([r[k] for r in rs if r[k] == r[k]]) for k in range(2, 7)]
        print('%-30s %-6s demo %.3f ms, app CPU %.0f, app GPU %.0f, WindowServer CPU %.0f, GPU %.0f ms/s (%d examples)'
              % (e, name, *vals, len(rs)))
