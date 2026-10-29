#!/usr/bin/env python3
"""Run the hand cleanup entry against native TST.B/BMI/CMP.W boundaries."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef unsigned char byte;
typedef unsigned short ushort;
typedef unsigned int uint, undefined4;
static byte work[0x4000], rom[0x20000];
#define W68PTR(a) (work + ((a) - 0xFFC000u))
#define ROM_BASE rom
static byte *_r_unaff_A6;
static int released, hidden, geometry;
static uint phase;
static uint release_palette_slot(void) { released++; return 0; }
static uint hide_object_sprite(void) { hidden++; return 0; }
static uint FUN_000195c6(int a, uint d0, uint d3, uint d6, uint d7)
{ geometry++; phase=d0; return 0; }
#include "sh_src/portfull/hand/FUN_00019596.c"
static void run(byte state, ushort x, int expect_clear)
{
  memset(work,0,sizeof(work)); released=hidden=geometry=0;
  _r_unaff_A6=W68PTR(0xFFD580u);
  *(ushort *)_r_unaff_A6=0x8800;
  *(ushort *)(_r_unaff_A6+12)=x;
  _r_unaff_A6[0x23]=255;
  *W68PTR(0xFFD420u)=state;
  *(ushort *)W68PTR(0xFFF132u)=0x1400;
  FUN_00019596(0,0,0);
  assert(released==expect_clear && hidden==expect_clear);
  assert(geometry==!expect_clear);
  if(expect_clear) assert(*(ushort *)_r_unaff_A6==0);
  else { assert(*(ushort *)_r_unaff_A6==0x8800); assert(phase==0); }
}
int main(void)
{
  run(0,0x1400,0);       /* live head, in view */
  run(0x7f,0x1400,0);    /* positive TST.B boundary */
  run(0x80,0x1400,1);    /* negative TST.B boundary */
  run(0xff,0x1400,1);    /* native boss death state */
  run(0,0x13df,1);       /* before camera-32 */
  run(0,0x13e0,1);       /* equal: native BCS does not continue */
  run(0,0x13e1,0);       /* just beyond cutoff */
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='codex-boss-cleanup-') as tmp:
    p = Path(tmp); (p/'test.c').write_text(source)
    subprocess.run(['cc','-std=c99','-Wno-pointer-to-int-cast','-I',str(root),
                    str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: seven native signed-state/camera-cutoff cases, real hand function')
