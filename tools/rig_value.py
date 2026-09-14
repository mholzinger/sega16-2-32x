#!/usr/bin/env python3
"""READ THE VALUE CHANNEL OFF THE MiSTer RIG.

The 68K floods CRAM with one colour per vint; the rig's only instrument
is a screenshot of that flood.  Nine-bit path (ECHO/STAMP/TAIL/CONSUME/
TRIP census):  blue = d9>>6 & 7, green = d9>>3 & 7, red = d9 & 7, so
    d9 = ((b>>5) << 6) | ((g>>5) << 3) | (r>>5)
from an 8-bit PNG channel.

THE RIG RATE-LIMITS SCREENSHOTS TO ABOUT ONE PER SIX SECONDS -- shots
closer than that come back as the previous image, which reads as a
perfectly repeatable measurement.  Default spacing is 8s.  Nothing here
decides what the number MEANS; --layout only splits the bits.

  tools/rig_value.py -n 12                  raw d9
  tools/rig_value.py -n 12 --layout trip    [8:7] tag, [6:0] value
  tools/rig_value.py -n 12 --layout census  [8:6] tag, [5:0] value
"""
import subprocess, sys, time, argparse, collections, os
HOST="root@mister.office.local"
SHOTS="/media/fat/screenshots/S32X"
TRIP=["gens","releases","fallbacks","presented"]

def sh(c): return subprocess.run(["ssh",HOST,c],capture_output=True,text=True).stdout.strip()

def shoot():
    sh("echo screenshot > /dev/MiSTer_cmd")

def newest():
    return sh(f"ls -t {SHOTS}/*.png 2>/dev/null | head -1")

def fetch(remote, local):
    subprocess.run(["scp","-q",f"{HOST}:{remote}",local],check=True)

def decode(path):
    """Modal colour of the frame -- the flood covers the screen, but the
    32X layer may paint over part of it, so take the mode, not a pixel."""
    from PIL import Image
    im=Image.open(path).convert("RGB")
    im=im.resize((80,60))                      # cheap, keeps the mode
    c=collections.Counter(im.getdata()).most_common(1)[0]
    r,g,b=c[0]
    return (((b>>5)&7)<<6) | (((g>>5)&7)<<3) | ((r>>5)&7), c[0], c[1]/4800.0

ap=argparse.ArgumentParser()
ap.add_argument("-n",type=int,default=8)
ap.add_argument("--gap",type=float,default=8.0)
ap.add_argument("--layout",choices=["raw","trip","census"],default="raw")
ap.add_argument("--tmp",default="/tmp/rigval")
a=ap.parse_args()
os.makedirs(a.tmp,exist_ok=True)

seen=collections.defaultdict(list); last=None
for i in range(a.n):
    if i: time.sleep(a.gap)
    shoot(); time.sleep(1.2)
    rp=newest()
    if rp==last:
        print(f"  {i:2d} STALE (rig returned the same file) -- widen --gap"); continue
    last=rp
    lp=os.path.join(a.tmp,os.path.basename(rp))
    fetch(rp,lp)
    d9,rgb,frac=decode(lp)
    if a.layout=="trip":
        t,v=(d9>>7)&3, d9&127
        print(f"  {i:2d} d9={d9:03X} rgb={rgb} cover={frac:.0%}  tag {t} ({TRIP[t]}) = {v}")
        seen[TRIP[t]].append(v)
    elif a.layout=="census":
        t,v=(d9>>6)&7, d9&63
        print(f"  {i:2d} d9={d9:03X} rgb={rgb} cover={frac:.0%}  tag {t} = {v}")
        seen[t].append(v)
    else:
        print(f"  {i:2d} d9={d9:03X} rgb={rgb} cover={frac:.0%}")
if seen:
    print("\n  per tag:")
    for k,v in seen.items():
        print(f"    {str(k):12s} n={len(v):2d}  {v}")
