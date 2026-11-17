#!/usr/bin/env python3
"""Synthetic SH links prove optional worker gaps and preserve existing fences.

Uses only temporary objects/scripts, never shared make or a game ROM build.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDK = Path(os.environ.get('SHBIN', str(Path.home()/'src/marsdev/mars/sh-elf/bin')))
AS = str(SDK/'sh-elf-as')
LD = str(SDK/'sh-elf-ld')
OBJCOPY = str(SDK/'sh-elf-objcopy')
OBJDUMP = str(SDK/'sh-elf-objdump')


def command(args, *, success=True):
    r = subprocess.run(args, cwd=ROOT, text=True, capture_output=True)
    if success:
        assert r.returncode == 0, (args, r.stdout, r.stderr)
    return r


with tempfile.TemporaryDirectory(prefix='worker-layout-') as tmp:
    tmp = Path(tmp)
    # Execute only the existing sed recipe, with all outputs redirected to tmp.
    # This also works on fresh checkouts without ignored sh_src/mars4.ld.
    lines = (ROOT/'Makefile').read_text().splitlines()
    start = lines.index('sh_src/mars4.ld: sh_src/mars.ld') + 1
    recipe = []
    for line in lines[start:]:
        if not line.startswith('\t'):
            break
        recipe.append(line[1:])
    assert recipe and recipe[0].startswith('sed ')
    base = tmp/'mars4.ld'
    script = '\n'.join(recipe).replace('$<', shlex.quote(str(ROOT/'sh_src/mars.ld'))).replace('$@', shlex.quote(str(base)))
    command(['/bin/sh', '-c', script])
    linker = tmp/'mars4s.ld'
    command([sys.executable, str(ROOT/'tools/gen_ld_ssf.py'), str(base), str(linker)])
    text = linker.read_text()
    assert text.count('ASSERT(__romimg_end <= 0x00234000,') == 2
    assert 'ADDR(.sfxdata) + SIZEOF(.sfxdata) <= 0x0229E100' in text
    assert 'ADDR(.artbake3) + SIZEOF(.artbake3) <= 0x0229E100' in text
    assert '0x02300000 + SIZEOF(.portcode) <= 0x02380000' in text
    baseline = tmp/'without-workers.ld'
    without = re.sub(r'\t/\* R60_WORKER_ROM_BEGIN:.*?\t/\* R60_WORKER_ROM_END \*/\n', '', text, flags=re.S)
    assert without != text and '.r60worker' not in without
    baseline.write_text(without)

    def link(name, *, worker=0, probe=0, mdart=0x51e0, sfx=0x31688,
             zero_sections=False, worker_subsection=False, script=linker,
             text_bytes=4, workset=None, art1=0, portcode=0, expect=None):
        source = tmp/(name+'.s')
        sections = [('.text', text_bytes, 'ax'), ('.ramtext', 4, 'ax'),
                    ('.mdsprart', mdart, 'a'), ('.sfxdata', sfx, 'a'),
                    ('.portdata', 16, 'a'), ('.artbake3', 16, 'a')]
        if workset is not None:
            sections.append(('.spriteworkset', workset, 'a'))
        if art1:
            sections.append(('.artbake1', art1, 'a'))
        if portcode:
            sections.append(('.portcode', portcode, 'a'))
        if worker or zero_sections:
            sections.append(('.r60worker.idle' if worker_subsection else '.r60worker', worker, 'ax'))
        if probe or zero_sections:
            sections.append(('.r60workerprobe', probe, 'ax'))
        source.write_text('\n'.join(f'.section {section},"{flags}"\n.space {size},0x31' for section, size, flags in sections)+'\n')
        obj = source.with_suffix('.o')
        command([AS, '--small', str(source), '-o', str(obj)])
        elf = source.with_suffix('.elf')
        r = command([LD, '-T', str(script), '--defsym', '_MMIO_BASE=0x0602C700',
                     '-o', str(elf), str(obj)], success=expect is None)
        if expect is not None:
            assert r.returncode and expect in r.stderr, (name, expect, r.stderr)
            return None
        binary = source.with_suffix('.bin')
        command([OBJCOPY, '-O', 'binary', '--gap-fill=0xff', str(elf), str(binary)])
        return elf, binary.read_bytes()

    # Exact capacities with both adjacent owner ends touching the new starts.
    elf, data = link('exact', worker=0xd00, probe=0x970, mdart=0x5200, sfx=0x31690)
    assert data[0x2a3300:0x2a4000] == b'1'*0xd00
    assert data[0x297690:0x298000] == b'1'*0x970
    headers = command([OBJDUMP, '-h', str(elf)]).stdout
    assert re.search(r'\.r60worker\s+00000d00\s+022a3300\s+002a3300', headers)
    assert re.search(r'\.r60workerprobe\s+00000970\s+02297690\s+00297690', headers)
    # A function subsection must land in the pinned region, not an orphan.
    subelf, _ = link('subsection', worker=4, worker_subsection=True)
    subheaders = command([OBJDUMP, '-h', str(subelf)]).stdout
    assert re.search(r'\.r60worker\s+00000004\s+022a3300\s+002a3300', subheaders)
    assert '.r60worker.idle' not in subheaders
    # One independently bounded physical mapper page; neither normal level
    # pages nor the accepted global-art window may be displaced to fit it.
    wself, wsdata = link('working_set_exact', workset=0x80000, art1=0xb000)
    assert wsdata[0x380000:0x400000] == b'1' * 0x80000
    assert wsdata[0x375000:0x380000] == b'1' * 0xb000
    wsheaders = command([OBJDUMP, '-h', str(wself)]).stdout
    assert re.search(r'\.spriteworkset\s+00080000\s+02380000\s+00380000', wsheaders)
    link('working_set_short', workset=0x7ffff,
         expect='.spriteworkset must contain exactly one 512KiB page')
    link('working_set_over', workset=0x80001,
         expect='.spriteworkset must contain exactly one 512KiB page')
    link('working_set_code_overlap', workset=0x80000, portcode=0x80001,
         expect='.portcode overlaps physical working-set page 7')
    without_ws = tmp/'without-working-set.ld'
    without_ws.write_text(re.sub(r'\t\.spriteworkset .*?(?=\t\.pages )', '', text, flags=re.S))
    assert '.spriteworkset' not in without_ws.read_text()
    _, wsabsent = link('working_set_absent', art1=0xb000)
    _, wsbaseline = link('working_set_old_layout', art1=0xb000, script=without_ws)
    _, wsempty = link('working_set_empty_input', art1=0xb000, workset=0)
    assert wsabsent == wsbaseline == wsempty, 'empty working-set allocation changes normal image'
    link('worker_over', worker=0xd01, expect='.r60worker exceeds its 3328-byte fixed allocation')
    link('probe_over', probe=0x971, expect='.r60workerprobe exceeds its 2416-byte fixed allocation')
    link('mdart_growth', worker=4, mdart=0x5201, expect='.mdsprart grew into .r60worker')
    link('sfx_growth', probe=4, sfx=0x31691, expect='.sfxdata grew into .r60workerprobe')
    # Check the next owners' ACTUAL addresses, not just hardcoded capacities.
    earlier_port = tmp/'earlier-port.ld'
    earlier_port.write_text(text.replace('.portdata 0x022A4000 : AT(0x002A4000)',
                                        '.portdata 0x022A3FF0 : AT(0x002A3FF0)'))
    link('port_moved', worker=0xd00, script=earlier_port,
         expect='.r60worker overflows into .portdata')
    earlier_art = tmp/'earlier-art.ld'
    earlier_art.write_text(text.replace('.artbake3 0x02298000 : AT(0x00298000)',
                                       '.artbake3 0x02297FF0 : AT(0x00297FF0)'))
    link('art_moved', probe=0x970, script=earlier_art,
         expect='.r60workerprobe overflows into .artbake3')
    # Old owner growth remains legal when optional sections are absent/empty.
    # Compare actual linked ROM bytes against the same old layout without them.
    for zero in (False, True):
        suffix = 'zero' if zero else 'absent'
        _, now = link('normal_'+suffix, mdart=0x5ef0, sfx=0x31ff0, zero_sections=zero)
        _, before = link('baseline_'+suffix, mdart=0x5ef0, sfx=0x31ff0,
                         zero_sections=zero, script=baseline)
        assert now == before, suffix+' optional-section layout changed normal image bytes'
    # Original ROM load-image fence still rejects overflow; no cap was raised.
    link('rom_image_over', text_bytes=0x234000,
         expect='.text+.data+.ramtext ROM image reached .sounddata')
    link('old_art3_guard', sfx=0x32001,
         expect='.sfxdata grew into the baked-art window 3')
print('resident layout: exact3328/2416B helpers and512KiB working set; adjacent/overflow fences, empty-image identity pass')
