#!/usr/bin/env python3
"""sh_src/mars4s.ld from sh_src/mars4.ld: the CARTSSF=1 layout. KIT.

The SSF2 mapper (docs/design/SILICON.md 4e) pages 512 KB slots of the low 4 MB. The music
stream (sh_src/pages_data.s, cart 0x400000 onward) is read through slot 7, so slot 7
(0x380000-0x3FFFFF) must hold nothing else. On the CARTMB=4 line .portcode's tail sits there,
so the SSF layout repacks the low 4 MB (sprites.bin trimmed by 176 KB, sh_src/sprites_data.s):

    .text and the art            ..0x234000
    .sounddata (speech)      0x234000..0x266000
    .sfxdata                 0x266000..0x29D000
    .palscenes / .mdsprart   0x29D000..0x2A4000   (68K: bank 2, window offset 0x9E100)
    .portdata                0x2A4000..0x2B0C00
    .opmrows                 0x2B0C00..0x2C0000   (R60OPMLANE: the synth lane's rows 48 KB + the engine's tables 11.5 KB)
    .portrom                 0x2C0000..0x300000   (256 KB aligned: the port masks & 0x3FFFF)
    .portcode                0x300000..0x380000
    slot 7                   0x380000..0x400000   THE WINDOW, empty
    .pages                   0x400000..           the stream bank (or the gate's markers)

Every move is an exact text replacement with its count asserted, so a drifted mars.ld fails
here instead of linking something else.
"""
import sys

WINOFF = 0x9E100     # .mdsprart's offset in the 68K's bank-2 window on this layout (bake_mdspr.py --winoff)

if sys.argv[1:] == ['--winoff']:    # the Makefile asks: one source for the layout and the bake
    print('0x%X' % WINOFF)
    sys.exit(0)
if len(sys.argv) < 3:
    raise SystemExit("usage: gen_ld_ssf.py SRC DST [--workset-clipram=1|2] [--workset-code=4096|8192|fixed] [--launcher]")
clipram = codebytes = 0
fixedcode = launcher = False
for option in sys.argv[3:]:
    if option == '--launcher' and not launcher:
        launcher = True
        continue
    if option.startswith('--workset-clipram='):
        if clipram or option not in ('--workset-clipram=1','--workset-clipram=2'):
            raise SystemExit("workset clip placement must be1(RAM) or2(ROM control)")
        clipram=int(option[-1])
    elif option in ('--workset-code=4096', '--workset-code=8192') and not (codebytes or fixedcode):
        codebytes=int(option.rsplit('=', 1)[1])
    elif option == '--workset-code=fixed' and not (codebytes or fixedcode):
        fixedcode=True
    else:
        raise SystemExit("workset code mode must be4096,8192 or fixed; no duplicate or unknown options")
src, dst = sys.argv[1], sys.argv[2]
s = open(src).read()
edits = [
    ("LENGTH = 0x00400000", "LENGTH = 0x01000000"),
    ("ASSERT(__romimg_end <= 0x00260000,", "ASSERT(__romimg_end <= 0x00234000,"),
    (".sounddata 0x02260000 : AT(0x00260000)", ".sounddata 0x02234000 : AT(0x00234000)"),
    ("ASSERT(ADDR(.sounddata) + SIZEOF(.sounddata) <= 0x02292000,",
     "ASSERT(ADDR(.sounddata) + SIZEOF(.sounddata) <= 0x02266000,"),
    (".sfxdata 0x02292000 : AT(0x00292000)", ".sfxdata 0x02266000 : AT(0x00266000)"),
    # .palscenes is EMPTY in the port build (sh-elf-size: absent), so the bank runs to .mdsprart at
    # 0x29E100: 229632 B. Neff's footstep (arc_B2, 2026-11-02) took the bank to 226324 B, 1044 past 0x29D000.
    ('ASSERT(ADDR(.sfxdata) + SIZEOF(.sfxdata) <= 0x022F8000, ".sfxdata overflows into .palscenes at 0x2F8000")',
     'ASSERT(ADDR(.sfxdata) + SIZEOF(.sfxdata) <= 0x0229E100, ".sfxdata overflows into .mdsprart at 0x29E100")'),
    (".palscenes 0x022F8000 : AT(0x002F8000)", ".palscenes 0x0229D000 : AT(0x0029D000)"),
    (".mdsprart 0x022F9100 : AT(0x002F9100)", ".mdsprart 0x0229E100 : AT(0x0029E100)"),
    (".portrom 0x02300000 : AT(0x00300000)", ".portrom 0x022C0000 : AT(0x002C0000)"),
    (".portdata 0x02340000 : AT(0x00340000)", ".portdata 0x022A4000 : AT(0x002A4000)"),
    (".portcode 0x0234D000 : AT(0x0034D000)", ".portcode 0x02300000 : AT(0x00300000)"),
    ("ASSERT(SIZEOF(.portdata) <= 0xD000,", "ASSERT(SIZEOF(.portdata) <= 0xCC00,"),   # .opmrows starts at 0x2B0C00
    ("ASSERT(0x0234D000 + SIZEOF(.portcode) <= 0x02400000",
     "ASSERT(0x02300000 + SIZEOF(.portcode) <= 0x02380000"),
]
for a, b in edits:
    n = s.count(a)
    want = 2 if a.startswith("ASSERT(__romimg_end") else 1     # mars.ld carries that guard twice
    assert n == want, f"gen_ld_ssf: anchor found {n} times, not {want}: {a!r}"
    s = s.replace(a, b)
pages = ("\t.spriteworkset 0x02380000 : AT(SIZEOF(.spriteworkset) == 0 ? 0 : 0x00380000) { KEEP(*(.spriteworkset)) } > rom\n"
         "\tASSERT(SIZEOF(.spriteworkset) == 0 || SIZEOF(.spriteworkset) == 0x80000, \".spriteworkset must contain exactly one 512KiB page\")\n"
         "\tASSERT(SIZEOF(.spriteworkset) == 0 || ADDR(.portcode) + SIZEOF(.portcode) <= ADDR(.spriteworkset), \".portcode overlaps physical working-set page 7\")\n"
         "\tASSERT(ADDR(.spriteworkset) + SIZEOF(.spriteworkset) <= 0x02400000, \".spriteworkset overlaps normal level pages\")\n"
         "\t.pages 0x02400000 : AT(0x00400000) { KEEP(*(.pages)) } > rom\n"
         "\tASSERT(DEFINED(_launcher_page_blob) ? ((_launcher_page_blob & 0x7FFFF) == 0 && _launcher_page_blob >= 0x02400000) : 1, \"the launcher page must start on a 512 KB page above the game pages\")\n"
         "\t/* the SH-2 synth lane's register rows (retired): after .portdata, under .portrom */\n"
         "\t.opmrows 0x022B0C00 : AT(0x002B0C00) { KEEP(*(.opmrows)) } > rom\n"
         "\tASSERT(ADDR(.opmrows) + SIZEOF(.opmrows) <= 0x022C0000, \".opmrows overflows into .portrom at 0x2C0000\")\n")
if codebytes:
    codeoff = 0x80000 - 64 - codebytes
    codeaddr = 0x02380000 + codeoff
    # Claim this owned object's complete closure BEFORE generic text/rodata/data
    # wildcards can steal an outlined helper or silently allocate writable state.
    # One page output owns prefix, code, FF capacity padding and the old guard.
    codepage = (
        "\t/* Optional page7 slave closure; only callable under its GO/page lease. */\n"
        "\t.spriteworkset 0x02380000 : AT(0x00380000) {\n"
        "\t\tKEEP(*(.spriteworkset.prefix))\n"
        f"\t\tASSERT(. - ADDR(.spriteworkset) == 0x{codeoff:X}, \"working-set code prefix must end at reserved offset\");\n"
        "\t\t__r60_ws_code_start = .;\n"
        "\t\tKEEP(*r60_sprite_blocks_code.o(.r60wscomp.text))\n"
        "\t\t__r60_ws_entry_end = .;\n"
        "\t\tASSERT(__r60_ws_entry_end > __r60_ws_code_start, \"working-set code entry section empty\");\n"
        "\t\tKEEP(*r60_sprite_blocks_code.o(.text .text.* .rodata .rodata.*))\n"
        "\t\t. = ALIGN(4); __r60_ws_code_end = .;\n"
        f"\t\tASSERT(__r60_ws_code_end > __r60_ws_code_start && __r60_ws_code_end - __r60_ws_code_start <= {codebytes}, \"working-set code empty or exceeds reserved cap\");\n"
        "\t\t. = 0x80000;\n"
        "\t} > rom = 0xFFFFFFFF\n"
        "\t__r60_ws_code_load = LOADADDR(.spriteworkset) + __r60_ws_code_start - ADDR(.spriteworkset);\n"
        "\t__r60_ws_code_size = __r60_ws_code_end - __r60_ws_code_start;\n"
        "\t__r60_ws_code_limit = 0x023FFFC0;\n"
        "\tASSERT(SIZEOF(.spriteworkset) == 0x80000, \".spriteworkset must contain exactly one 512KiB page\")\n"
        "\tASSERT(LOADADDR(.spriteworkset) == 0x00380000, \"working-set code physical page anchor\")\n"
        f"\tASSERT(__r60_ws_code_start == 0x{codeaddr:08X} && (__r60_ws_code_start & 3) == 0, \"working-set code placement/alignment\")\n"
        "\tASSERT(__r60_ws_code_end <= __r60_ws_code_limit, \"working-set code overlaps64-byte guard\")\n"
        "\tASSERT(DEFINED(_r60_sb_code_run) || DEFINED(_r60_tp_code_run) || DEFINED(_r60_mc_code_draw_packet_ready), \"working-set code entry missing\")\n"
        "\tASSERT(DEFINED(_r60_sb_code_run) ? _r60_sb_code_run == __r60_ws_code_start : 1, \"block entry must be first\")\n"
        "\tASSERT(DEFINED(_r60_tp_code_run) ? _r60_tp_code_run == __r60_ws_code_start : 1, \"transition entry must be first\")\n"
        "\tASSERT(DEFINED(_r60_mc_code_draw_packet_ready) ? _r60_mc_code_draw_packet_ready == __r60_ws_code_start : 1, \"macro entry must be first\")\n"
        "\t/DISCARD/ : { *r60_sprite_blocks_code.o(.comment .note.GNU-stack .debug* .stab* .gnu.attributes) }\n"
        "\t.r60wsreject 0 (INFO) : { *r60_sprite_blocks_code.o(*) *r60_sprite_blocks_code.o(COMMON) *(.spriteworkset) }\n"
        "\tASSERT(SIZEOF(.r60wsreject) == 0, \"working-set code writable/orphan sections or full-page input forbidden\")\n"
    )
    assert s.count('SECTIONS {') == 1
    s = s.replace('SECTIONS {', 'SECTIONS {\n' + codepage)
    oldpage = pages[:pages.index('\tASSERT(SIZEOF(.spriteworkset) == 0 || ADDR(.portcode)')]
    pages = pages.replace(oldpage, '', 1)
anchor = "\t.portrom 0x022C0000 : AT(0x002C0000) {"
assert s.count(anchor) == 1
s = s.replace(anchor, pages + anchor)
# THE BAKED-ART WINDOWS (R60BAKEART, tools/bake_art.py, 2026-11-04): the SH-2-readable gaps of
# this layout. Window 0 is the tail of page 8 behind the music blob (sh_src/pages_data.s, slot 7
# shows page 8 from boot); windows 1-3 are the straight gaps below the slot-7 window (the gap
# above .text is NOT one: the .data/.ramtext load images sit there, __romimg_end ~0x231600). Each is
# pinned, and each ASSERT fires only when the window is in use, so a rom without the bake is
# unchanged. The addresses are the --chunks the Makefile hands the bake; change both together.
artwin = (
    "\t.artbake1 0x02375000 : AT(0x00375000) { KEEP(*(.artbake1)) } > rom\n"
    "\tASSERT(SIZEOF(.artbake1) == 0 || 0x02300000 + SIZEOF(.portcode) <= 0x02375000, \".portcode grew into the baked-art window 1 at 0x375000\")\n"
    "\tASSERT(ADDR(.artbake1) + SIZEOF(.artbake1) <= 0x02380000, \".artbake1 overflows into the slot-7 window at 0x380000\")\n"
    "\t.artbake2 0x022B1000 : AT(0x002B1000) { KEEP(*(.artbake2)) } > rom\n"
    "\tASSERT(SIZEOF(.artbake2) == 0 || SIZEOF(.opmrows) == 0, \".artbake2 and .opmrows (R60OPMLANE) both want 0x2B1000\")\n"
    "\tASSERT(SIZEOF(.artbake2) == 0 || ADDR(.portdata) + SIZEOF(.portdata) <= 0x022B1000, \".portdata grew into the baked-art window 2 at 0x2B1000\")\n"
    "\tASSERT(ADDR(.artbake2) + SIZEOF(.artbake2) <= 0x022C0000, \".artbake2 overflows into .portrom at 0x2C0000\")\n"
    "\t.artbake3 0x02298000 : AT(0x00298000) { KEEP(*(.artbake3)) } > rom\n"
    "\tASSERT(SIZEOF(.artbake3) == 0 || ADDR(.sfxdata) + SIZEOF(.sfxdata) <= 0x02298000, \".sfxdata grew into the baked-art window 3 at 0x298000\")\n"
    "\tASSERT(ADDR(.artbake3) + SIZEOF(.artbake3) <= 0x0229E100, \".artbake3 overflows into .mdsprart at 0x29E100\")\n")
# Optional worker/probe code stays resident in low slots4/5, never the paged
# slot7 window. These gaps are bounded by their real neighboring owners; do
# not borrow space by weakening the existing load-image/art/audio fences.
# No input sections when disabled: ld drops these output sections. Explicitly
# zero-sized inputs use LMA0, so objcopy cannot split/erase a preceding owner
# that legitimately grew through an unused gap (the old empty-art-window bug).
worker = (
    "\t/* R60_WORKER_ROM_BEGIN: optional resident code, no normal-build payload. */\n"
    "\t.r60worker 0x022A3300 : AT(SIZEOF(.r60worker) == 0 ? 0 : 0x002A3300) { KEEP(*(.r60worker .r60worker.*)) } > rom\n"
    "\tASSERT(SIZEOF(.r60worker) <= 0xD00, \".r60worker exceeds its 3328-byte fixed allocation\")\n"
    "\tASSERT(SIZEOF(.r60worker) == 0 || ADDR(.mdsprart) + SIZEOF(.mdsprart) <= ADDR(.r60worker), \".mdsprart grew into .r60worker at 0x2A3300\")\n"
    "\tASSERT(SIZEOF(.r60worker) == 0 || ADDR(.r60worker) + SIZEOF(.r60worker) <= ADDR(.portdata), \".r60worker overflows into .portdata at 0x2A4000\")\n"
    "\t.r60workerprobe 0x02297690 : AT(SIZEOF(.r60workerprobe) == 0 ? 0 : 0x00297690) { KEEP(*(.r60workerprobe .r60workerprobe.*)) } > rom\n"
    "\tASSERT(SIZEOF(.r60workerprobe) <= 0x970, \".r60workerprobe exceeds its 2416-byte fixed allocation\")\n"
    "\tASSERT(SIZEOF(.r60workerprobe) == 0 || ADDR(.sfxdata) + SIZEOF(.sfxdata) <= ADDR(.r60workerprobe), \".sfxdata grew into .r60workerprobe at 0x297690\")\n"
    "\tASSERT(SIZEOF(.r60workerprobe) == 0 || ADDR(.r60workerprobe) + SIZEOF(.r60workerprobe) <= ADDR(.artbake3), \".r60workerprobe overflows into .artbake3 at 0x298000\")\n"
    "\t/* R60_WORKER_ROM_END */\n"
)
if clipram:
    vma = '0x0603E800' if clipram==1 else '0x02297690'
    region = 'ram' if clipram==1 else 'rom'
    worker += (
        "\t/* Shared clip code uses ONLY the disabled worker's768-byte payload. */\n"
        f"\t.r60clipcode {vma} : AT(0x00297690) {{ __r60_clip_start = .; KEEP(*(.r60clipcode)) . = ALIGN(4); __r60_clip_end = .; }} > {region}\n"
        "\t__r60_clip_size = SIZEOF(.r60clipcode);\n"
        "\t__r60_clip_load = 0x02000000 + LOADADDR(.r60clipcode);\n"
        "\tASSERT(SIZEOF(.r60clipcode) > 0 && SIZEOF(.r60clipcode) <= 0x300, \"shared clip code exceeds768-byte worker payload\")\n"
        "\tASSERT((ADDR(.r60clipcode) & 3) == 0 && (LOADADDR(.r60clipcode) & 3) == 0, \"shared clip code alignment\")\n"
        "\tASSERT(_r60_wc_rows == ADDR(.r60clipcode), \"shared clip entry is not first byte\")\n"
        "\tASSERT(SIZEOF(.r60workerprobe) == 0, \"shared clip load image conflicts with resident probe\")\n"
        "\tASSERT(ADDR(.sfxdata) + SIZEOF(.sfxdata) <= 0x02297690, \"SFX overlaps shared clip load image\")\n"
        "\tASSERT(0x02297690 + SIZEOF(.r60clipcode) <= ADDR(.artbake3), \"shared clip load image overlaps art\")\n"
        "\tASSERT(0x0603E800 + SIZEOF(.r60clipcode) <= 0x0603EB00, \"shared clip code overlaps mailbox\")\n"
    )
s = s.replace(anchor, artwin + worker + anchor)
if fixedcode:
    owner = '*r60_sprite_blocks_code.o'
    # These earlier wildcards must not consume any part of the owned object.
    # Preserve the existing portfull exclusion while excluding this object too.
    first = 'EXCLUDE_FILE(sh_src/portfull/*.o) *('
    assert s.count(first) == 1
    s = s.replace(first, f'EXCLUDE_FILE(sh_src/portfull/*.o {owner}) @@WSSTAR@@(')
    s = s.replace('*(', f'EXCLUDE_FILE({owner}) *(').replace('@@WSSTAR@@(', '*(')
    fixed = (
        "\n\t/* Optional readonly slave closure in the existing ROM-load gap. */\n"
        "\t__r60_ws_code_load = ALIGN(__romimg_end, 64);\n"
        "\t.r60wscode (0x02000000 + __r60_ws_code_load) : AT(__r60_ws_code_load) {\n"
        "\t\t__r60_ws_code_start = .;\n"
        "\t\tKEEP(*r60_sprite_blocks_code.o(.r60wscomp.text))\n"
        "\t\t__r60_ws_entry_end = .;\n"
        "\t\tASSERT(__r60_ws_entry_end > __r60_ws_code_start, \"working-set code entry section empty\");\n"
        "\t\tKEEP(*r60_sprite_blocks_code.o(.text .text.* .rodata .rodata.*))\n"
        "\t\t. = ALIGN(4); __r60_ws_code_end = .;\n"
        "\t} > rom\n"
        "\t__r60_ws_code_size = __r60_ws_code_end - __r60_ws_code_start;\n"
        "\t__r60_ws_code_limit = 0x02234000;\n"
        "\tASSERT(__r60_ws_code_size > 0 && __r60_ws_code_end <= __r60_ws_code_limit, \"fixed working-set code exceeds existing ROM-load gap\")\n"
        "\tASSERT((__r60_ws_code_start & 63) == 0 && LOADADDR(.r60wscode) >= __romimg_end, \"fixed working-set code alignment/overlap\")\n"
        "\tASSERT(DEFINED(_r60_sb_code_run), \"working-set code entry missing\")\n"
        "\tASSERT(DEFINED(_r60_sb_code_run) ? _r60_sb_code_run == __r60_ws_code_start : 1, \"working-set code entry must be first\")\n"
        "\t/DISCARD/ : { *r60_sprite_blocks_code.o(.comment .note.GNU-stack .debug* .stab* .gnu.attributes) }\n"
        "\t.r60wsreject 0 (INFO) : { *r60_sprite_blocks_code.o(*) *r60_sprite_blocks_code.o(COMMON) }\n"
        "\tASSERT(SIZEOF(.r60wsreject) == 0, \"working-set code writable/orphan sections forbidden\")\n"
    )
    end = '\t__romimg_end = __ramtext3_lma + __ramtext3_size;'
    assert s.count(end) == 1
    s = s.replace(end, end + fixed)
if launcher:
    # THE LAUNCHER'S PAGE-SWITCH CODE (sh_src/r60/r60_launcher.c): run from the 4 KB probe scrap
    # at 0x06038000, loaded from what is left of the rom image's slot under .sounddata. The
    # launcher copies it there itself, so the boot copy table is untouched.
    assert not fixedcode, "--launcher and --workset-code=fixed both want the gap above __romimg_end"
    lt = (
        "\n\t.launchtext 0x06038000 : AT(ALIGN(__romimg_end, 4)) {\n"
        "\t\t_launchtext_start = .;\n"
        "\t\tKEEP(*(.launchtext .launchtext.*))\n"
        "\t\t. = ALIGN(4); _launchtext_end = .;\n"
        "\t} > ram\n"
        "\t_launchtext_load = 0x02000000 + LOADADDR(.launchtext);\n"
        "\tASSERT(LOADADDR(.launchtext) + SIZEOF(.launchtext) <= 0x00234000, \".launchtext's load image reached .sounddata at 0x234000\")\n"
        "\tASSERT(_launchtext_end <= 0x06039000, \".launchtext outgrew the 4 KB probe scrap at 0x06038000\")\n"
        "\tASSERT(__ramtext_end <= 0x06038000, \".ramtext reached .launchtext at 0x06038000\")\n"
    )
    end = '\t__romimg_end = __ramtext3_lma + __ramtext3_size;'
    assert s.count(end) == 1
    s = s.replace(end, end + lt)
open(dst, "w").write(s)
print(f"{dst}: the CARTSSF layout, slot 7 free")
