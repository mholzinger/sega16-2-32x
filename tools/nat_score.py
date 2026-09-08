#!/usr/bin/env python3
import subprocess, sys, struct, os
ARES="/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless"
REPO="/Users/mikeholzinger/src/sega16-2-32x"
SCR=os.path.dirname(os.path.abspath(__file__))
rom=sys.argv[1]; frames=int(sys.argv[2]) if len(sys.argv)>2 else 1900
dp,wp=SCR+"/d_ns.bin",SCR+"/w_ns.bin"
hp=SCR+"/h_ns.bin"
subprocess.run([ARES,"--frames",str(frames),"--input",REPO+"/discover/inputs/play2.csv",
 "--dump",f"sdram:0x28000:0x1000:{dp}","--dump",f"wram:0xFFA000:0x2000:{wp}","--dump",f"sdram:0x39800:0x200:{hp}",rom],check=True,capture_output=True)
d=open(dp,"rb").read(); w=open(wp,"rb").read()
def D(i): return struct.unpack(">I",d[i*4:i*4+4])[0]
def W(i): return struct.unpack(">I",d[0xF50+i*4:0xF50+i*4+4])[0]
def w16(a): return struct.unpack(">H",w[a-0xA000:a-0xA000+2])[0]
def w32(a): return struct.unpack(">I",w[a-0xA000:a-0xA000+4])[0]
v=w16(0xB0F0); c=D(9)
print(f"{os.path.basename(rom)}: ships {D(28)} ({D(28)*60/c:.1f}fps) wall {W(0)/max(1,W(1))/12052:.2f}v max {W(2)/12052:.2f} "
 f"| overrun {D(30)} holds {D(29)} wedges {D(27)} | bad1 {w16(0xA0B2)} rej {100*w16(0xB0FC)/v:.2f}% skips {D(7)} "
 f"| hdlr {w32(0xB0D0)/v:.1f} wa {w32(0xA040)/v:.1f} | psw {W(3)} heal {w16(0xA0D6)}")
ph=[D(0x390+i) for i in range(16)]
if ph[10] or ph[0]:
    g=max(1,W(1)); f=max(1,ph[10]); V=12052
    print(f"  phase(v/gen): echo {ph[0]/g/V:.2f} (max {ph[5]/V:.2f}) mtask {ph[1]/g/V:.2f} (max {ph[6]/V:.2f}) "
          f"lag {ph[2]/g/V:.2f} (max {ph[7]/V:.2f}) ship {ph[3]/g/V:.2f} (max {ph[8]/V:.2f}) "
          f"flip {ph[4]/f/V:.2f} (max {ph[9]/V:.2f}) | gens {W(1)} flips {ph[10]}")
    hd=open(hp,"rb").read(); h=struct.unpack(">16H",hd[0x100:0x120]); per=struct.unpack(">4H",hd[0x120:0x128])
    print(f"  ship period bins (1/2/3/4+ vints): {per}  -> {100*per[0]/max(1,sum(per)):.0f}% single-vint")
    acks=v
    print(f"  window: landing {D(0x3FE)/max(1,D(0x3FF))/V:.2f}v blit-done {D(0x39B)/g/V:.2f}v ack {D(0x38F)/acks/V:.2f}v (n={acks}) launch {D(0x38E)/g/V:.2f}v  (offsets into the vint) ")
    print(f"  echo bins 0.5..1.5v/0.125: {' '.join(str(x) for x in h[:8])} | mtask bins: {' '.join(str(x) for x in h[8:])}")
