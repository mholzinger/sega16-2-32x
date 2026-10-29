import sys, subprocess, os, struct
A=os.path.expanduser('~/src/ares-debug/build_macos/headless-ui/Release/ares-headless')
rom=sys.argv[1]; frames=[int(x) for x in sys.argv[2:]];
import romsyms; LST=romsyms.lst_for(rom)   # symbols move per build: the map proven to be this rom's
def sym(n):
    for ln in open(LST):
        if ln.rstrip().endswith(' _'+n): return int(ln.split()[0],16)-0x06000000
    raise SystemExit('no symbol '+n)
SH=sym('r60_nt_shadow'); CE=sym('r60_census'); print(f'shadow @ {SH:#x} census @ {CE:#x} (from s16.lst: must be this rom build)')
out=os.environ.get('ACCEPT_OUT', os.path.dirname(os.path.abspath(__file__))+'/../build/accept')+'/shv_'+os.path.basename(rom); os.makedirs(out,exist_ok=True)
def rdw(p): d=open(p,'rb').read(); return [int.from_bytes(d[i:i+2],'big') for i in range(0,len(d),2)]
procs=[]
for F in frames:
    procs.append(subprocess.Popen([A]+(['--input',os.environ['SHV_INPUT']] if os.environ.get('SHV_INPUT') else [])+['--frames',str(F+1),'--dump',f'32X SDRAM:{SH:#x}:0x2000:{out}/sh{F}.bin','--dump',f'VDP VRAM:0xC000:0x4000:{out}/v{F}.bin','--dump',f'32X SDRAM:{CE:#x}:4:{out}/t{F}.bin','--dump',f'32X SDRAM:0x28E3A:4:{out}/q{F}.bin','--dump',f'wram:0xFFA070:0x40:{out}/w{F}.bin','--dump',f'32X SDRAM:0x28FE0:4:{out}/g{F}.bin','--dump',f'32X SDRAM:0x28E40:0x10:{out}/d{F}.bin',rom],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    if len(procs)>=7: [p.wait() for p in procs]; procs=[]
[p.wait() for p in procs]
for F in frames:
    sh=rdw(f'{out}/sh{F}.bin'); v=rdw(f'{out}/v{F}.bin'); t=rdw(f'{out}/t{F}.bin'); q=rdw(f'{out}/q{F}.bin'); w=rdw(f'{out}/w{F}.bin'); g=rdw(f'{out}/g{F}.bin'); d=struct.unpack('>4I',open(f'{out}/d{F}.bin','rb').read())
    res=[]
    for which,base in ((0,0),(1,0x1000)):   # plane A = shadow[0] vs VRAM C000; plane B = shadow[1] vs E000
        diff=[]
        for r in range(32):
            n=sum(1 for c in range(64) if sh[which*2048+r*64+c]!=v[base+r*64+c] and sh[which*2048+r*64+c]!=0xFFFF)
            if n: diff.append(f'{r}:{n}')
        res.append('AB'[which]+'['+(' '.join(diff) if diff else 'ok')+']')
    print(f'{F}: hold {g[0]} SEQ {q[0]} DONE {q[1]} flips {d[1]} 68K A {w[3]} skipA {w[21]} tornM {t[0]} tornS {t[1]} | '+' '.join(res))
