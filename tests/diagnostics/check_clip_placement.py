#!/usr/bin/env python3
"""Link real temporary SH objects against both optional clip placements.

Protects the existing worker payload/FIFO boundary and load-image owners. No
game assets, ignored build files, shared make, or game ROM build are required.
SHBIN follows check_worker_layout.py and may select another SDK installation.
"""
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDK = Path(os.environ.get('SHBIN', str(Path.home()/'src/marsdev/mars/sh-elf/bin')))
AS, LD, OBJCOPY = (str(SDK/('sh-elf-'+name)) for name in ('as', 'ld', 'objcopy'))


def command(args, *, error=None):
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True)
    if error is None:
        assert result.returncode == 0, (args, result.stdout, result.stderr)
    else:
        assert result.returncode and error in result.stderr, (args, error, result.stderr)
    return result


def elf(path):
    """ELF section/load/symbol metadata, independent of objdump formatting."""
    data = path.read_bytes()
    assert data[:6] == b'\x7fELF\x01\x02'
    header = struct.unpack_from('>HHIIIIIHHHHHH', data, 16)
    sections = [struct.unpack_from('>10I', data, header[5]+i*header[10])
                for i in range(header[11])]
    names = sections[header[12]]
    strings = data[names[4]:names[4]+names[5]]
    named = {strings[s[0]:].split(b'\0', 1)[0].decode(): s for s in sections}
    loads = [struct.unpack_from('>8I', data, header[4]+i*header[8])
             for i in range(header[9])]
    symbols = {}
    for table in sections:
        if table[1] != 2:
            continue
        names = sections[table[6]]
        strings = data[names[4]:names[4]+names[5]]
        for offset in range(table[4], table[4]+table[5], table[9]):
            name, value, _, _, _, _ = struct.unpack_from('>IIIBBH', data, offset)
            symbols[strings[name:].split(b'\0', 1)[0].decode()] = value
    return data, named, loads, symbols


with tempfile.TemporaryDirectory(prefix='clip-placement-') as directory:
    tmp = Path(directory)
    # Use the actual canonical-base recipe, including on fresh checkouts where
    # sh_src/mars4.ld has not been generated. All outputs stay temporary.
    lines = (ROOT/'Makefile').read_text().splitlines()
    recipe = []
    for line in lines[lines.index('sh_src/mars4.ld: sh_src/mars.ld')+1:]:
        if not line.startswith('\t'):
            break
        recipe.append(line[1:])
    assert recipe and recipe[0].startswith('sed ')
    base = tmp/'base.ld'
    script = '\n'.join(recipe).replace('$<', shlex.quote(str(ROOT/'sh_src/mars.ld')))
    command(['/bin/sh', '-c', script.replace('$@', shlex.quote(str(base)))])
    scripts = {}
    for mode in (0, 1, 2):
        output = tmp/f'placement{mode}.ld'
        args = [sys.executable, str(ROOT/'tools/gen_ld_ssf.py'), str(base), str(output)]
        command(args + ([f'--workset-clipram={mode}'] if mode else []))
        scripts[mode] = output
    assert '.r60clipcode' not in scripts[0].read_text()
    assert '__r60_clip_' not in scripts[0].read_text()
    for bad in ('0', '3', '1 2'):
        command([sys.executable, str(ROOT/'tools/gen_ld_ssf.py'), str(base),
                 str(tmp/'invalid.ld'), '--workset-clipram='+bad],
                error='workset clip placement must be')

    serial = 0

    def link(mode, *, size=None, prefix=0, probe=0, sfx=0, linker=None,
             error=None, gc=False):
        global serial
        serial += 1
        path = tmp/f'case{serial}'
        source = '.section .text,"ax"\n.global _start\n_start:rts\nnop\n.long boot_ram_keep\n'
        source += '.section .ramtext,"ax"\nboot_ram_keep:rts\nnop\n'
        source += '.section .bss,"aw",@nobits\n.space 4\n'
        source += '.section .ramtext2,"ax"\nrts\nnop\n'
        if size is not None:
            source += '.section .r60clipcode,"ax"\n.align 2\n'
            source += f'.space {prefix}\n.global _r60_wc_rows\n_r60_wc_rows:\n'
            source += f'.space {size-prefix},0x31\n'
        if probe:
            source += f'.section .r60workerprobe,"ax"\n.space {probe}\n'
        if sfx:
            source += f'.section .sfxdata,"a"\n.space {sfx}\n'
        path.with_suffix('.s').write_text(source)
        command([AS, '--isa=sh2', '-big', str(path.with_suffix('.s')),
                 '-o', str(path.with_suffix('.o'))])
        output = path.with_suffix('.elf')
        args = [LD, '-T', str(linker or scripts[mode]), '--defsym',
                '_MMIO_BASE=0x0602C700', '-e', '_start']
        if gc:
            args.append('--gc-sections')
        command(args + [str(path.with_suffix('.o')), '-o', str(output)], error=error)
        return output if error is None else None

    for mode, vma in ((1, 0x0603e800), (2, 0x02297690)):
        for size in (4, 768):
            path = link(mode, size=size)
            data, sections, loads, symbols = elf(path)
            section = sections['.r60clipcode']
            assert section[3] == vma and section[5] == size and section[8] >= 4
            owner = [p for p in loads if p[0] == 1 and p[2] <= vma
                     and vma+size <= p[2]+p[4]]
            assert len(owner) == 1 and owner[0][3]+vma-owner[0][2] == 0x297690
            assert symbols['_r60_wc_rows'] == symbols['__r60_clip_start'] == vma
            assert symbols['__r60_clip_end'] == vma+size
            assert symbols['__r60_clip_size'] == size
            assert symbols['__r60_clip_load'] == 0x02297690
            binary = path.with_suffix('.bin')
            command([OBJCOPY, '-O', 'binary', '--gap-fill=0xff', str(path), str(binary)])
            assert binary.read_bytes()[0x297690:0x297690+size] == b'1'*size
        kept = link(mode, size=4, gc=True)
        assert elf(kept)[1]['.r60clipcode'][5] == 4, 'KEEP lost an unreferenced body'
        link(mode, size=772, error='exceeds768-byte worker payload')
        link(mode, size=0, error='exceeds768-byte worker payload')
        link(mode, size=8, prefix=4, error='entry is not first byte')
        link(mode, size=4, probe=4, error='conflicts with resident probe')
        link(mode, size=4, sfx=0x31694, error='SFX overlaps shared clip load image')
        for kind, old, new in (
            ('VMA', f'.r60clipcode 0x{vma:08X}', f'.r60clipcode 0x{vma+2:08X}'),
            ('LMA', 'AT(0x00297690)', 'AT(0x00297692)'),
        ):
            changed = tmp/f'misaligned-{mode}-{kind}.ld'
            text = scripts[mode].read_text()
            # Generator prints the ROM VMA with uppercase hex as well.
            assert text.count(old) == 1, (kind, old)
            changed.write_text(text.replace(old, new))
            link(mode, size=4, linker=changed, error='shared clip code alignment')

    # Feature-off links allocate no new load image, even with an empty input
    # section. Compare actual binary bytes, rather than linker-script wording.
    binaries = []
    for size in (None, 0):
        path = link(0, size=size)
        binary = path.with_suffix('.bin')
        command([OBJCOPY, '-O', 'binary', '--gap-fill=0xff', str(path), str(binary)])
        binaries.append(binary.read_bytes())
        assert '.r60clipcode' not in elf(path)[1]
    assert binaries[0] == binaries[1], 'empty feature-off input changed normal image'

print('clip placement: RAM/ROM load mapping, KEEP, 768-byte cap, entry/alignment/owner fences and feature-off image identity pass')
