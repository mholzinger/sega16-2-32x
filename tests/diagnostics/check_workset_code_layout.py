#!/usr/bin/env python3
"""Temporary real SH objects protect the opt-in page-code closure and fences.

No game assets, shared make or game ROM build. The actual standalone compositor
is also compiled privately with synthetic reservation metadata, never called.
"""
import importlib.util
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
import sys as _sys; _sys.path.insert(0, str(ROOT/'tools')); from mktext import makefile_text
SDK = Path(os.environ.get('SHBIN', str(Path.home()/'src/marsdev/mars/sh-elf/bin')))
AS, LD, CC, OBJCOPY = (str(SDK/('sh-elf-'+n)) for n in ('as', 'ld', 'gcc', 'objcopy'))
spec = importlib.util.spec_from_file_location('page_checker', ROOT/'tools/check_workset_code.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def command(args, *, cwd=ROOT, error=None):
    result = subprocess.run(args, cwd=cwd, text=True, capture_output=True)
    if error is None:
        assert result.returncode == 0, (args, result.stdout, result.stderr)
    else:
        assert result.returncode and error in result.stderr, (args, error, result.stderr)
    return result


def sections(path):
    data = path.read_bytes()
    header = struct.unpack_from('>HHIIIIIHHHHHH', data, 16)
    entries = [struct.unpack_from('>10I', data, header[5]+i*header[10]) for i in range(header[11])]
    s = entries[header[12]]
    names = data[s[4]:s[4]+s[5]]
    return {names[s[0]:].split(b'\0', 1)[0].decode(): (s, data[s[4]:s[4]+s[5]]) for s in entries}


def load_addresses(path):
    data = path.read_bytes()
    header = struct.unpack_from('>HHIIIIIHHHHHH', data, 16)
    loads = [struct.unpack_from('>8I', data, header[4]+i*header[8]) for i in range(header[9])]
    result = {}
    for name, (section, _) in sections(path).items():
        if section[1] != 1 or not section[2]&2 or not section[5]:
            continue
        owner = [p for p in loads if p[0] == 1 and p[2] <= section[3]
                 and section[3]+section[5] <= p[2]+p[4]]
        assert len(owner) == 1, (name, owner)
        result[name] = owner[0][3]+section[3]-owner[0][2]
    return result


with tempfile.TemporaryDirectory(prefix='workset-code-') as directory:
    tmp = Path(directory)
    # Fresh-checkout canonical linker base, without any ignored generated files.
    lines = makefile_text(str(ROOT)).splitlines()
    recipe = []
    for line in lines[lines.index('sh_src/mars4.ld: sh_src/mars.ld')+1:]:
        if not line.startswith('\t'):
            break
        recipe.append(line[1:])
    assert recipe and recipe[0].startswith('sed ')
    base = tmp/'base.ld'
    text = '\n'.join(recipe).replace('$<', shlex.quote(str(ROOT/'sh_src/mars.ld')))
    command(['/bin/sh', '-c', text.replace('$@', shlex.quote(str(base)))])
    default, active, fixed = tmp/'default.ld', tmp/'active.ld', tmp/'fixed.ld'
    for path, options in ((default, []), (active, ['--workset-code=8192']),
                          (fixed, ['--workset-code=fixed'])):
        command([sys.executable, str(ROOT/'tools/gen_ld_ssf.py'), str(base), str(path), *options])
    assert '.r60wsreject' not in default.read_text() and '__r60_ws_code_' not in default.read_text()
    assert active.read_text().count('\t.spriteworkset ') == 1
    assert active.read_text().index('.spriteworkset ') < active.read_text().index('.text 0x02000000')
    for bad in ('0', '4096', '8193', '8192 8192'):
        command([sys.executable, str(ROOT/'tools/gen_ld_ssf.py'), str(base), str(tmp/'bad.ld'),
                 '--workset-code='+bad], error='workset code mode must be8192 or fixed')
    command([sys.executable, str(ROOT/'tools/gen_ld_ssf.py'), str(base), str(tmp/'duplicate.ld'),
             '--workset-code=8192', '--workset-code=8192'], error='no duplicate')
    command([sys.executable, str(ROOT/'tools/gen_ld_ssf.py'), str(base), str(tmp/'mixed.ld'),
             '--workset-code=8192', '--workset-code=fixed'], error='no duplicate')

    page = bytes((i*31+7)&255 for i in range(0x7aa40))+b'\xff'*(0x80000-0x7aa40)
    (tmp/'r60_workingset.bin').write_bytes(page)

    def assemble_pages(name, enabled):
        output = tmp/(name+'.o')
        args = [AS, '--isa=sh2', '-big', '--defsym', 'CART_SSF=1', '--defsym', 'R60_WORKSET=1']
        if enabled:
            args += ['--defsym', 'R60_WORKSET_CODE=8192']
        command([*args, str(ROOT/'sh_src/pages_data.s'), '-o', str(output)], cwd=tmp)
        return output

    pages_off, pages_on = assemble_pages('pages-off', False), assemble_pages('pages-on', True)
    off, on = sections(pages_off), sections(pages_on)
    assert off['.spriteworkset'][1] == page
    assert on['.spriteworkset.prefix'][1] == page[:0x7dfc0]
    assert '.spriteworkset' not in on and '.spriteworkset.prefix' not in off
    assert off['.pages'][1] == on['.pages'][1]
    for defs, error in ((['CART_SSF=1'], 'requires R60_WORKSET'),
                        (['R60_WORKSET=1'], 'requires CART_SSF'),
                        (['CART_SSF=1', 'R60_WORKSET=1', 'R60_WORKSET_CODE=4096'], 'must be8192')):
        args = [AS, '--isa=sh2', '-big']
        for value in defs:
            args += ['--defsym', value]
        if not any(v.startswith('R60_WORKSET_CODE=') for v in defs):
            args += ['--defsym', 'R60_WORKSET_CODE=8192']
        command([*args, str(ROOT/'sh_src/pages_data.s'), '-o', str(tmp/'bad-pages.o')], cwd=tmp, error=error)

    stub = tmp/'stub.s'
    stub.write_text('.section .text,"ax"\n.global _start\n_start:rts\nnop\n.long boot_ram\n'
                    '.global ___udivsi3\n___udivsi3:rts\nnop\n'
                    '.global forbidden_external\nforbidden_external:rts\nnop\n'
                    '.section .ramtext,"ax"\nboot_ram:rts\nnop\n'
                    '.section .bss,"aw",@nobits\n.space 4\n')
    command([AS, '--isa=sh2', '-big', str(stub), '-o', str(tmp/'stub.o')])
    serial = 0

    def object_(size=4, *, prefix=0, extra='', entry=True):
        global serial
        serial += 1
        folder = tmp/f'case{serial}'
        folder.mkdir()
        source = '.section .r60wscomp.text,"ax"\n.balign 4\n'+f'.space {prefix}\n'
        if entry:
            source += '.global _r60_sb_code_run\n.type _r60_sb_code_run,@function\n_r60_sb_code_run:\n'
        source += f'.space {size-prefix},0x31\n'
        if entry:
            source += '.size _r60_sb_code_run,.-_r60_sb_code_run\n'
        source += extra
        path = folder/'r60_sprite_blocks_code.o'
        path.with_suffix('.s').write_text(source)
        command([AS, '--isa=sh2', '-big', str(path.with_suffix('.s')), '-o', str(path)])
        return path

    def link(obj=None, *, script=active, pages=pages_on, gc=False, error=None):
        folder = obj.parent if obj else tmp
        path = folder/('gc.elf' if gc else 'linked.elf')
        args = [LD, '-T', str(script), '--defsym', '_MMIO_BASE=0x0602C700', '-e', '_start']
        if gc:
            args.append('--gc-sections')
        command([*args, str(tmp/'stub.o'), str(pages), *([str(obj)] if obj else []),
                 '-o', str(path)], error=error)
        return path

    for size in (4, 8192):
        obj = object_(size)
        checker.inspect(obj)
        path = link(obj)
        s, body = sections(path)['.spriteworkset']
        assert s[3] == 0x02380000 and s[5] == 0x80000
        assert body[:0x7dfc0] == page[:0x7dfc0]
        assert body[0x7dfc0:0x7dfc0+size] == b'1'*size
        assert body[0x7dfc0+size:] == b'\xff'*(0x80000-0x7dfc0-size)
        binary = path.with_suffix('.bin')
        command([OBJCOPY, '-O', 'binary', '--gap-fill=0xff', str(path), str(binary)])
        assert binary.read_bytes()[0x380000:0x400000] == body
    kept = link(object_(), gc=True)
    assert sections(kept)['.spriteworkset'][1][0x7dfc0:0x7dfc4] == b'1'*4
    readonly = object_(extra='.section .text.helper,"ax"\n.space 4,0x32\n.section .rodata,"a"\n.long 0x12345678\n')
    checker.inspect(readonly)
    body = sections(link(readonly))['.spriteworkset'][1]
    assert body[0x7dfc0:0x7dfcc] == b'11112222\x12\x34\x56\x78'
    for obj, error in ((object_(8196), 'exceeds8192-byte cap'),
                        (object_(0), 'entry section empty'),
                        (object_(0, extra='.section .rodata,"a"\n.long 1\n'), 'entry section empty'),
                        (object_(8, prefix=4), 'entry must be first'),
                        (object_(entry=False), 'entry missing')):
        link(obj, error=error)
    for extra in ('.section .data,"aw"\n.long 1\n',
                  '.section .bss,"aw",@nobits\n.space 4\n',
                  '.comm common_state,4\n',
                  '.section .unexpected,"a"\n.long 1\n',
                  '.section .ctors,"a"\n.long 1\n',
                  '.section .gcc_except_table,"a"\n.long 1\n'):
        obj = object_(extra=extra)
        try:
            checker.inspect(obj)
        except ValueError:
            pass
        else:
            raise AssertionError('object checker accepted forbidden section')
        link(obj, error='writable/orphan sections')
    external = object_(extra='.long forbidden_external\n')
    try:
        checker.inspect(external)
    except ValueError as error:
        assert 'unexpected page external' in str(error)
    else:
        raise AssertionError('non-UDIV external accepted')
    # The checker is required even though an unrelated fixed symbol could link.
    link(external)
    # Actual SH partial-inplace DIR32 addends, not just symbol-table identity.
    for extra, error_text in (
        ('.global fixed_escape\n.set fixed_escape,0x02300000\n.long fixed_escape\n', 'ABS closure symbol'),
        ('.long local_owned+0x100000\n.section .rodata,"a"\nlocal_owned:.long 1\n', 'addend out of bounds'),
        ('.long ___udivsi3+4\n', 'division helper addend'),
        ('.long 0\n.reloc 4,R_SH_REL32,local_owned\n.section .rodata,"a"\nlocal_owned:.long 1\n', 'unsupported SH'),
    ):
        obj = object_(extra=extra)
        try:
            checker.inspect(obj)
        except ValueError as error:
            assert error_text in str(error), (error_text, str(error))
        else:
            raise AssertionError('unsafe ABS/relocation/addend accepted')
    valid_addend = object_(extra='.long local_owned+4\n.section .rodata,"a"\nlocal_owned:.long 1,2\n')
    assert checker.inspect(valid_addend)['checked_relocations'] == 1
    for offset in (-4, 4):
        fake = tmp/f'prefix{offset}.s'
        fake.write_text(f'.section .spriteworkset.prefix,"a"\n.space {0x7dfc0+offset},0xff\n')
        command([AS, '--isa=sh2', '-big', str(fake), '-o', str(fake.with_suffix('.o'))])
        link(object_(), pages=fake.with_suffix('.o'), error='prefix must end')
    link(object_(), pages=pages_off, error='prefix must end')
    moved = tmp/'moved-load.ld'
    moved.write_text(active.read_text().replace('AT(0x00380000)', 'AT(0x00380004)', 1))
    link(object_(), script=moved, error='physical page anchor')

    # Feature off still serializes the entire original baked page and has no
    # code symbol/closure or extra output allocation.
    normal_path = link(script=default, pages=pages_off)
    normal = sections(normal_path)
    assert normal['.spriteworkset'][1] == page and '.r60wsreject' not in normal

    # Fixed alternative owns the same closure after the real ROM-load end. It
    # leaves every existing allocated section and full baked page unchanged.
    fixed_obj = object_(extra='.section .text.helper,"ax"\n.space 4,0x32\n.section .rodata,"a"\n.long 0x12345678\n')
    fixed_path = link(fixed_obj, script=fixed, pages=pages_off)
    fixed_output = sections(fixed_path)
    before_load, after_load = load_addresses(normal_path), load_addresses(fixed_path)
    assert all(after_load[name] == address for name, address in before_load.items())
    for name, (section, data) in normal.items():
        if section[2]&2:
            assert name in fixed_output and fixed_output[name][0][3] == section[3]
            assert fixed_output[name][0][5] == section[5]
            if section[1] != 8:  # NOBITS has no serialized file bytes.
                assert fixed_output[name][1] == data, ('fixed placement moved/changed owner', name)
    fixed_section, fixed_body = fixed_output['.r60wscode']
    assert not fixed_section[3]&63 and fixed_body == b'11112222\x12\x34\x56\x78'
    near = tmp/'fixed-near-end.ld'
    near.write_text(fixed.read_text().replace('__romimg_end = __ramtext3_lma + __ramtext3_size;',
                                               '__romimg_end = 0x0023315C;', 1))
    exact = sections(link(object_(3712), script=near, pages=pages_off))['.r60wscode']
    assert exact[0][3] == 0x02233180 and exact[0][3]+exact[0][5] == 0x02234000
    link(object_(3716), script=near, pages=pages_off, error='exceeds existing ROM-load gap')
    for obj, error in ((object_(0), 'entry section empty'),
                        (object_(8, prefix=4), 'entry must be first'),
                        (object_(entry=False), 'entry missing')):
        link(obj, script=fixed, pages=pages_off, error=error)
    for extra in ('.section .data,"aw"\n.long 1\n',
                  '.section .unexpected,"a"\n.long 1\n',
                  '.section .ctors,"a"\n.long 1\n',
                  '.section .gcc_except_table,"a"\n.long 1\n'):
        link(object_(extra=extra), script=fixed, pages=pages_off, error='writable/orphan sections')

    metadata = tmp/'r60_workingset_data.h'
    metadata.write_text('#define SPRITE_WORKSET_PAGE_BYTES 0x80000u\n'
                        '#define SPRITE_WORKSET_GUARD_BYTES 64u\n'
                        '#define SPRITE_WORKSET_CODE_OFFSET 0x7DFC0u\n'
                        '#define SPRITE_WORKSET_CODE_ADDRESS 0x023FDFC0u\n'
                        '#define SPRITE_WORKSET_CODE_CAPACITY_BYTES 8192u\n')
    actual = tmp/'actual'
    actual.mkdir()
    obj = actual/'r60_sprite_blocks_code.o'
    args = [CC, '-x', 'c', '-m2', '-mb', '-O2', '-fomit-frame-pointer', '-fno-lto',
            '-ffreestanding', '-fno-pic', '-fno-pie', '-DCART_SSF', '-DR60_WORKSET',
            '-DR60_WORKSET_CODE=8192', '-I'+str(tmp), '-I'+str(ROOT/'sh_src/r60'),
            '-c', str(ROOT/'sh_src/r60/r60_sprite_blocks_code.inc')]
    command([*args, '-o', str(obj)])
    proof = checker.inspect(obj)
    assert proof['external_symbols'] == ['___udivsi3']
    closure = sections(link(obj))['.spriteworkset'][1]
    assert closure[-64:] == b'\xff'*64
    page_object_sections = sections(obj)
    fixed_actual = actual/'fixed'
    fixed_actual.mkdir()
    fixed_obj = fixed_actual/'r60_sprite_blocks_code.o'
    fixed_args = [x for x in args if x != '-DR60_WORKSET_CODE=8192']
    command([*fixed_args, '-DR60_WORKSET_CODE_FIXED=1', '-o', str(fixed_obj)])
    assert checker.inspect(fixed_obj)['external_symbols'] == ['___udivsi3']
    fixed_target = sections(link(fixed_obj, script=near, pages=pages_off))
    for name in ('.r60wscomp.text', '.rodata'):
        assert sections(fixed_obj)[name][1] == page_object_sections[name][1]
    assert fixed_target['.spriteworkset'][1] == page
    lto = actual/'lto.o'
    command([*args, '-flto', '-o', str(lto)])
    try:
        checker.inspect(lto)
    except ValueError as error:
        assert 'LTO' in str(error)
    else:
        raise AssertionError('LTO input accepted')

print('workset code: leased page8192 + fixed aligned ROM-gap closure, exact-end/overflow/default identity, owned helpers/tables, ABS/addend/foreign/LTO rejection and actual isolated compositor links pass')
