import sys,os
for f in sys.argv[1:]:
    b=open(f,'rb').read(); t=int.from_bytes(b[0x302a:0x302c],'big')
    out=[]
    for s in (20,21,16):
        o=s*0x80; x=b[o:o+0x80]
        out.append('%d: x %s dx %s st %s +7e %02x +2e %02x anim %s'%(s,x[0xc:0xe].hex(),x[0x14:0x16].hex(),x[0x20:0x24].hex(),x[0x7e],x[0x2e],x[0x24:0x28].hex()))
    print('%-14s tick %04x  %s'%(os.path.basename(f),t,' | '.join(out)))
