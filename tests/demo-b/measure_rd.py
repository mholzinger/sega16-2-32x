import sys, os, subprocess, struct, glob, re
rd=sys.argv[1]; S=os.path.dirname(os.path.abspath(__file__)); T=os.environ.get('DEMO_TICKS', S+'/arcade')
A=os.environ.get('ARES','/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless')
ROM=rd if rd.endswith('.32x') else '/Users/mikeholzinger/src/sega16-2-32x/rom/%s.32x'%rd
def dump(f, off, ln, out):
    subprocess.run([A, ROM, '--frames', str(f), '--dump', 'sdram:0x%x:0x%x:%s'%(off,ln,out)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
frames=[22,100,1492,1493,1495,1500,1510,1530,1540,1560,1650,1700,2000,2100,2150]
procs=[]
for f in frames:
    procs.append(subprocess.Popen([A, ROM, '--frames', str(f), '--dump', 'sdram:0x2CC00:0x4000:%s/%s_%d.bin'%(T,rd,f)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
for p in procs: p.wait()
W={f:open('%s/%s_%d.bin'%(T,rd,f),'rb').read() for f in frames}
live=lambda b:[i for i in range(64) if b[i*0x80]&0x80]
tick=lambda b:int.from_bytes(b[0x302a:0x302c],'big')
print('boot: f22 slot0 %s | f100 slot0 %s'%(W[22][:4].hex(), W[100][:4].hex()))
for f in (1492,1493,1495,1500):
    b=W[f]; o=0x1800
    print('f%d tick %02x slot48 +0 %s y %s +22 %02x +20 %02x | hero +0 %s'%(f,tick(b),b[o:o+2].hex(),b[o+0x10:o+0x12].hex(),b[o+0x22],b[o+0x20],b[:2].hex()))
arc={1508:1506,1518:1516,1538:1536,1548:1546,1568:1566,1658:1658,1708:1708,2008:2008}
for f,g in ((1510,1516),(1530,1536),(1540,1546),(1560,1566),(1650,1658),(1700,1708),(2000,2008)):
    b=W[f]; ga=T+'/aw_%d.bin'%g
    if not os.path.exists(ga): continue
    a=open(ga,'rb').read()
    d=sum(1 for o in range(0,0x4000,2) if b[o:o+2]!=a[o:o+2])
    print('port f%d tick %03x live %s hero x %s | arc f%d tick %03x live %s hero x %s | diff %d'%(f,tick(b),live(b),b[0xc:0xe].hex(),g,tick(a),live(a),a[0xc:0xe].hex(),d))
for f in (2100,2150):
    b=W[f]; print('f%d tick %03x state %s step %02x'%(f,tick(b),b[0x3028:0x302a].hex(),b[0x3031]))
