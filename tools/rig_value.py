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
import functools
print=functools.partial(print,flush=True)
HOST="root@mister.office.local"
SHOTS="/media/fat/screenshots/S32X"
TRIP=["gens","releases","fallbacks","presented"]

def sh(c): return subprocess.run(["ssh",HOST,c],capture_output=True,text=True).stdout.strip()

def shoot():
    sh("echo screenshot > /dev/MiSTer_cmd")

def newest(rom=None):
    """MiSTer names each screenshot after the loaded rom, which is the only
    defence against CROSS-ROM CONTAMINATION -- and it is needed. Stray
    samplers from earlier runs kept shooting this rig after a new rom was
    pushed, so /tmp/rigval ended up holding files from two builds at once.
    Pass --rom to accept only that build's shots."""
    pat = f"{SHOTS}/*-{rom}.png" if rom else f"{SHOTS}/*.png"
    return sh(f"ls -t {pat} 2>/dev/null | head -1")

def fetch(remote, local):
    subprocess.run(["scp","-q",f"{HOST}:{remote}",local],check=True)

def decode(path, floor=0.40):
    """THE FLOOD IS NOT THE WHOLE SCREEN. It writes CRAM entries 0-63,
    i.e. MD palette lines 0-1, so it colours only the MD tiles that use
    them -- the 32X framebuffer layer has its own palette and covers the
    rest. Take the most common NON-BLACK colour in the bottom band and
    REQUIRE it to cover `floor` of that band.

    The gate is the whole point. Without it the reader falls through to
    whatever small game sprite happens to be the next most common colour
    and returns a number: seven consecutive shots of a static title
    screen decoded as a flat 'presented = 64' off a 272-pixel patch of
    the INSERT COIN blocks. A stable wrong answer, which is the worst
    kind. Returns None when the flood is not on screen."""
    from PIL import Image
    im=Image.open(path).convert("RGB"); px=im.load(); W,H=im.size
    band=[px[x,y] for y in range(H-40,H) for x in range(W)]
    nb=[(v,n) for v,n in collections.Counter(band).most_common() if v!=(0,0,0)]
    if not nb: return None
    (r,g,b),n = nb[0]
    frac=n/float(len(band))
    if frac < floor: return None
    return (((b>>5)&7)<<6) | (((g>>5)&7)<<3) | ((r>>5)&7), (r,g,b), frac

ap=argparse.ArgumentParser()
ap.add_argument("-n",type=int,default=8)
ap.add_argument("--gap",type=float,default=8.0)
ap.add_argument("--layout",choices=["raw","trip","census"],default="raw")
ap.add_argument("--tmp",default="/tmp/rigval")
ap.add_argument("--floor",type=float,default=0.40)
ap.add_argument("--rom",help="accept only screenshots named for this rom (MiSTer names them after the loaded game) -- use it whenever a previous sampler might still be running")
a=ap.parse_args()
os.makedirs(a.tmp,exist_ok=True)

seen=collections.defaultdict(list); last=None
for i in range(a.n):
    if i: time.sleep(a.gap)
    shoot(); time.sleep(1.2)
    rp=newest(a.rom)
    if rp==last:
        print(f"  {i:2d} STALE (rig returned the same file) -- widen --gap"); continue
    last=rp
    lp=os.path.join(a.tmp,os.path.basename(rp))
    fetch(rp,lp)
    dec=decode(lp)
    if dec is None:
        print(f"  {i:2d} NO FLOOD on screen (32X layer covering it) -- skipped"); continue
    d9,rgb,frac=dec
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
