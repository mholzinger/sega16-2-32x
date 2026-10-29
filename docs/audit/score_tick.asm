003716  btst.b     #0x0,(0xfffff026).w
00371C  beq.b      0x00003720
00371E  rts        
003720  lea        (-0xfd8).w,A5
003724  lea        (-0x2000).w,A6
003728  btst.b     #0x0,(A5)
00372C  beq.b      0x00003730
00372E  bsr.b      0x0000374a
003730  clr.l      (0x4,A6)
003734  lea        (-0xfd7).w,A5
003738  lea        (-0x1f80).w,A6
00373C  btst.b     #0x0,(A5)
003740  beq.b      0x00003744
003742  bsr.b      0x0000374a
003744  clr.l      (0x4,A6)
003748  rts        
