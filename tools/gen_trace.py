#!/usr/bin/env python3
"""Decode the one-generation event trace (PHASECENSUS=1 builds).
    gen_trace.py <dump of sdram 0x39800:0x200>   (STR at +0xE0)
Slave events (phi/8 ticks): 1 pickup 2 band0 start 3 band end 4 blit-half
start 5 blit-half end 6 textcap start 7 textcap end 8 chain end.
Master events (phi/32 ticks, x4 here): 81 launch 82 window pickup
83 landing done 84 ack 85 echo seen 86 close 87 blit done 88 flip.
Clocks are aligned on (echo seen, chain end): master poll lag is <0.01v.
Times print in vints (48208 slave ticks) relative to the launch."""
import struct, sys
NAMES = {1:"S pickup",2:"S band0 start",3:"S band end",4:"S blit-half start",5:"S blit-half end",
         6:"S textcap start",7:"S textcap end",8:"S chain end",
         0x81:"M launch",0x82:"M window pickup",0x83:"M landing done",0x84:"M ack",
         0x85:"M echo seen",0x86:"M close",0x87:"M blit done",0x88:"M flip"}
d = open(sys.argv[1], "rb").read()
n = struct.unpack(">I", d[0xE0 + 23*4:0xE0 + 24*4])[0]
ev = [struct.unpack(">I", d[0xE0 + i*4:0xE0 + i*4 + 4])[0] for i in range(1, min(n, 19))]
stb = struct.unpack(">4I", d[0xE0 + 19*4:0xE0 + 23*4])
gens = struct.unpack(">I", open(sys.argv[2], "rb").read()[0xF54:0xF58])[0] if len(sys.argv) > 2 else 0
if gens: print(f"pass sums over {gens} gens (v/gen, all bands): clear+sprites {stb[1]/gens/48208:.3f} cat1 {stb[2]/gens/48208:.3f}")
V = 48208.0
# unwrap each clock separately (16-bit stamps, monotonic within the trace)
def unwrap(seq):
    out, last, base = [], None, 0
    for t in seq:
        if last is not None and t < last: base += 65536
        out.append(t + base); last = t
    return out
s_ev = [(e >> 16, e & 0xFFFF) for e in ev if (e >> 16) < 0x80]
m_ev = [(e >> 16, e & 0xFFFF) for e in ev if (e >> 16) >= 0x80]
s_t = unwrap([t for _, t in s_ev]); m_t = [4 * t for t in unwrap([t for _, t in m_ev])]
# align: slave chain end (8) == master echo seen (0x85)
try:
    off = s_t[[e for e, _ in s_ev].index(8)] - m_t[[e for e, _ in m_ev].index(0x85)]
except ValueError:
    off = 0
rows = [(t + off, NAMES.get(e, hex(e))) for (e, _), t in zip(m_ev, m_t)] + \
       [(t, NAMES.get(e, hex(e))) for (e, _), t in zip(s_ev, s_t)]
try:
    t0 = m_t[[e for e, _ in m_ev].index(0x81)] + off
except (ValueError, IndexError):
    t0 = rows[0][0] if rows else 0
print(f"trace entries: {n-1}  (times in vints after M launch; slave/master aligned on echo)")
for t, name in sorted(rows):
    print(f"  {(t - t0) / V:+6.3f}  {name}")
