#!/usr/bin/env python3
"""Execute all three actual packet display writes with a host VDP binding.

Each admitted packet must establish its requested reg1 value even when another
packet path or the frontend last wrote the register. Packet consumption, DMA
and hardware timing are separate target gates.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
main = (ROOT/'md_src/md_main.c').read_text()
shim = (ROOT/'md_src/r60_shim.h').read_text()
functions = []
for name, source, packet in [('full', main, 'sc'), ('empty_a', shim, 'ea'),
                              ('empty_b', shim, 'eb')]:
    start = source.index(f'uint8_t h = (uint8_t)(({packet}[1] >> 13) & 1u);')
    write = source.index('*(volatile uint16_t*)VDP_CTRL_PORT', start)
    end = source.index('\n', write)
    body = source[start:end]
    assert body.count('VDP_CTRL_PORT') == 1
    functions.append(f'static void {name}(uint16_t word){{\n'
                     f'volatile uint16_t {packet}[2]={{0,word}};\n'+body+'\n}\n')

prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
static volatile uint16_t reg1;
static unsigned writes;
static volatile uint16_t *register_address(void){writes++;return &reg1;}
#define VDP_CTRL_PORT ((uintptr_t)register_address())
'''
test = r'''
int main(void){
 void(*path[3])(uint16_t)={full,empty_a,empty_b};
 unsigned pairs=0,external_cases=0,headers=0;
 for(unsigned p=0;p<3;p++)for(unsigned q=0;q<3;q++){
  if(p==q)continue;
  unsigned before=writes;
  path[p](0);assert(reg1==0x8174);
  path[q](0x2000);assert(reg1==0x8134);
  path[p](0);assert(reg1==0x8174&&writes==before+3);
  pairs++;
 }
 /* Menus/other register writers are outside packet-path cache ownership. */
 for(unsigned p=0;p<3;p++)for(unsigned hold=0;hold<2;hold++){
  uint16_t packet=(uint16_t)(hold?0x2000:0);
  path[p](packet);
  reg1=hold?0x8174:0x8134; /* An intervening external display write. */
  unsigned before=writes;path[p](packet);
  assert(reg1==(hold?0x8134:0x8174)&&writes==before+1);
  external_cases++;
 }
 /* Only bit13 controls display; all existing IE0/DMA/other reg1 bits retain
  * their exact known packet values. Repeated requests still issue one write. */
 for(unsigned p=0;p<3;p++)for(unsigned word=0;word<65536;word++){
  unsigned before=writes;path[p]((uint16_t)word);
  assert(reg1==((word&0x2000)?0x8134:0x8174)&&writes==before+1);headers++;
 }
 assert(pairs==6&&external_cases==6&&headers==196608);
 printf("PASS actual display paths: %u cross-path releases, %u external/menu writes, %u packet headers; exact reg1 values and one write per consumed packet\n",pairs,external_cases,headers);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='display-hold-') as directory:
    path = Path(directory)
    code = path/'check.c'
    code.write_text(prefix+'\n'.join(functions)+test)
    exe = path/'check'
    subprocess.run([os.environ.get('CC', 'clang'), '-std=c99', '-O2', '-Wall',
                    '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(code), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
