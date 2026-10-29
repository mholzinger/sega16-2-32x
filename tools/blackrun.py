# blackrun.py rom|arcade span_start:span_end ...   -> per span: frames whose active area is >=95% black, as runs
import sys, os, subprocess, numpy as np
from PIL import Image
sys.path.insert(0, os.path.expanduser('~/src/sega16-2-32x/.claude/worktrees/advisor-bisect/tools'))
import active_area as AA
A=os.path.expanduser('~/src/ares-debug/build_macos/headless-ui/Release/ares-headless')
ARC=os.path.expanduser('~/src/sega16-2-32x/.claude/worktrees/advisor-bisect/build/attract/arc6k')
src=sys.argv[1]; spans=[tuple(int(x) for x in s.split(':')) for s in sys.argv[2:]]
out=os.environ.get('ACCEPT_OUT', os.path.dirname(os.path.abspath(__file__))+'/../build/accept')+'/br_'+os.path.basename(src); os.makedirs(out,exist_ok=True)
def black(path, arcade):
    im=Image.open(path).convert('RGB')
    if not arcade: im=AA.crop_active(im,(320,224),Image.NEAREST)
    a=np.asarray(im); return float((a.sum(axis=2)<24).mean())
for s0,s1 in spans:
    if src=='arcade': vals={f:black(f'{ARC}/ref_{f:06d}.png',True) for f in range(s0,s1)}
    else:
        cmd=[A,'--frames',str(s1)]
        for f in range(s0,s1): cmd+=['--screenshot',f'{f}:{out}/f{f:06d}.png']
        subprocess.run(cmd+[src],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        vals={f:black(f'{out}/f{f:06d}.png',False) for f in range(s0,s1)}
    runs=[]; cur=None
    for f in range(s0,s1):
        b=vals[f]>=0.95
        if b and cur is None: cur=f
        if not b and cur is not None: runs.append((cur,f-1)); cur=None
    if cur is not None: runs.append((cur,s1-1))
    print(f'{os.path.basename(src)} {s0}-{s1}: black runs '+', '.join(f'{a}-{b} ({b-a+1})' for a,b in runs))
