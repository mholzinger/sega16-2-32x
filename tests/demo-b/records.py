import sys
def rec(b,s):
    o=s*0x80; r=b[o:o+0x80]
    return '%2d +0 %s h %s spr %s pal %s x %s y %s +1a %s st %s anim %s fr %s lnk %s hp %02x form %02x'%(s,r[0:2].hex(),r[2:6].hex(),r[8:10].hex(),r[10:12].hex(),r[0xc:0xe].hex(),r[0x10:0x12].hex(),r[0x1a:0x1c].hex(),r[0x20:0x24].hex(),r[0x24:0x28].hex(),r[0x26:0x28].hex(),r[0x3c:0x3e].hex(),r[0x4a],r[0x6a])
for arg in sys.argv[1:]:
    f,slots=arg.split(':'); b=open(f,'rb').read()
    print('==',f.split('/')[-1],'tick %04x'%int.from_bytes(b[0x302a:0x302c],'big'))
    for s in slots.split(','): print(rec(b,int(s)))
