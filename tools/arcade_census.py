import struct, sys, os, glob
D = sys.argv[1]
def rd(p): return open(p,'rb').read()
def w16(b,o): return struct.unpack('>H', b[o:o+2])[0]
frames = sorted(int(os.path.basename(p)[5:11]) for p in glob.glob(D+'/tile_*.bin'))
seen_vis = set(); seen_all = set()
print("frame bank1 | FGpages BGpages fgx fgy bgx bgy | vis(code,set) new_vis cum_vis | allpages_distinct cum_all | text_distinct")
prev_vis=None
for f in frames:
    T = rd('%s/tile_%06d.bin'%(D,f)); X = rd('%s/text_%06d.bin'%(D,f)); W = rd('%s/wram_%06d.bin'%(D,f))
    bank1 = W[0x95]
    vis=set(); allp=set()
    def decode(w):
        code = w & 0x1FFF
        if code & 0x1000: code = (code & 0xFFF) + bank1*0x1000
        return code, (w>>6)&0x7F
    for which in (0,1):
        pages = w16(X, (0x740+which)*2); xraw = w16(X,(0x74C+which)*2); ysc = w16(X,(0x748+which)*2)&0x1FF
        pq=[min(pages&0xF,12), min((pages>>4)&0xF,12), min((pages>>8)&0xF,12), min((pages>>12)&0xF,12)]
        vx0 = (0xC0 - (xraw&0x3FF)) & 0x3FF
        for r in range(28):
            vy = (ysc + r*8) & 0x1FF
            trow=(vy>>3)&0x1F; qy=(vy>>7)&2
            for c in range(41):
                vx=((vx0&~7)+c*8)&0x3FF
                p=pq[qy+((vx>>9)&1)]
                if p>=12: continue
                w=w16(T, (p*0x800 + trow*64 + ((vx>>3)&0x3F))*2)
                vis.add(decode(w))
        if which==0: fgi=(pages,vx0,ysc)
        else: bgi=(pages,vx0,ysc)
    for p in range(12):
        for i in range(0x800):
            w=w16(T,(p*0x800+i)*2)
            if w: allp.add(decode(w))
    newv = len(vis-seen_vis); seen_vis|=vis; seen_all|=allp
    txt=set(w16(X,i*2)&0x1FF for i in range(0x700))
    print("%5d %02X | %04X %04X %3d %3d %3d %3d | %4d %4d %5d | %5d %5d | %3d"%(f,bank1,fgi[0],bgi[0],fgi[1],fgi[2],bgi[1],bgi[2],len(vis),newv,len(seen_vis),len(allp),len(seen_all),len(txt)))
