#!/usr/bin/env python3
# Usage: scripts/demorunner_analyze.py <output directory of demorunner_sweep.sh>
# Per test and engine: fps, CPU ms per frame (DemoRunner, WindowServer) and GPU ms per frame (the
# union of each process's GPU intervals in the engine's trace), plus the median per engine.
import sys, subprocess, re, datetime, statistics as st
import xml.etree.ElementTree as ET
import os
S=os.path.join(sys.argv[1], '')
def rows(path):
    refs={}
    for ev,el in ET.iterparse(path,events=('end',)):
        if el.tag=='row':
            vals=[]
            for c in el:
                if 'ref' in c.attrib: c=refs.get(c.get('ref'),c)
                else:
                    for sub in c.iter():
                        if 'id' in sub.attrib: refs[sub.get('id')]=sub
                vals.append(c)
            yield vals; el.clear()
def union(iv):
    iv.sort(); tot=0; cs=ce=None
    for s,e in iv:
        if cs is None: cs,ce=s,e
        elif s<=ce: ce=max(ce,e)
        else: tot+=ce-cs; cs,ce=s,e
    if cs is not None: tot+=ce-cs
    return tot
data={}
for l in open(S+'windows.txt'):
    p=l.strip().split('|')
    if len(p)<9: continue
    data.setdefault(int(p[0]),[]).append(p)
out=[]
for i,tests in sorted(data.items()):
    T=S+f'engine{i}.trace'
    toc=subprocess.run(['xcrun','xctrace','export','--input',T,'--toc'],capture_output=True,text=True).stdout
    m=re.search(r'<start-date>([^<]+)</start-date>',toc)
    start=datetime.datetime.fromisoformat(m.group(1)).timestamp()
    x=S+f'engine{i}_intervals.xml'
    with open(x,'w') as f:
        subprocess.run(['xcrun','xctrace','export','--input',T,'--xpath','/trace-toc/run[@number="1"]/data/table[@schema="metal-gpu-intervals"]'],stdout=f,stderr=subprocess.DEVNULL)
    iv={'app':[],'ws':[]}
    for c in rows(x):
        proc=next((y.get('fmt') or '' for y in c if y.tag=='process'),'')
        key='app' if 'DemoRunner' in proc else ('ws' if 'WindowServer' in proc else None)
        if key is None or c[0].text is None or c[1].text is None: continue
        s=int(c[0].text)/1e9+start; e=s+int(c[1].text)/1e9
        iv[key].append((s,e))
    for p in tests:
        _,sel,test,t0,t1,cpuA,cpuW,fps,eng=p
        t0=float(t0); t1=float(t1); dur=t1-t0
        try: f=float(fps)
        except: f=float('nan')
        frames=f*dur
        def busy(k):
            clipped=[(max(s,t0),min(e,t1)) for s,e in iv[k] if e>t0 and s<t1]
            return union(clipped)*1000
        ga=busy('app'); gw=busy('ws')
        out.append((eng,test,f,float(cpuA)/frames,float(cpuW)/frames,ga/frames,gw/frames,sel))
import json; json.dump(out,open(S+'results.json','w'))
engs=["CoreGraphics Renderer","Metal Renderer","OpenGL Renderer","Software Renderer","CoreGraphics Metal Layer Renderer"]
short={"CoreGraphics Renderer":"CG","Metal Renderer":"Metal","OpenGL Renderer":"OpenGL","Software Renderer":"SW","CoreGraphics Metal Layer Renderer":"CG-MetalLayer"}
print('mismatch:',[(o[0],o[1],o[7]) for o in out if o[0]!=o[7]])
for e in engs:
    print('==',short[e],'  test | fps | CPU app ms/f | CPU WS ms/f | GPU app ms/f | GPU WS ms/f')
    for o in out:
        if o[0]==e: print('  %-24s %5.1f %7.2f %7.2f %7.2f %7.2f'%(o[1],o[2],o[3],o[4],o[5],o[6]))

print()
for e in engs:
    r=[o for o in out if o[0]==e]
    if not r: continue
    med=lambda k: st.median(o[k] for o in r)
    print('%-34s median ms/frame: app CPU %5.2f  app GPU %5.2f  WindowServer CPU %5.2f  WindowServer GPU %5.2f' % (e, med(3), med(5), med(4), med(6)))
