# -*- coding: utf-8 -*-
"""THE TABLES SCHEMA — one declaration per recurring 68K idiom.

    python3 -m kit.schema                 # the catalogue, by layer
    python3 -m kit.schema check           # audit every games/*/game.py
    python3 -m kit.schema check goldnaxe  # audit one
    python3 -m kit.schema index           # regenerate kit/INDEX.md

WHY THIS FILE EXISTS. Every key of `tools/game_<name>.py` TABLES names a
PATTERN -- an idiom that recurs across 68K arcade programs -- and the game
file is that title's instantiation. That catalogue was never declared, so it
drifted the moment a second title was derived (2026-10-04 measurement):

    altbeast          38 keys          goldnaxe          61 keys
    shared            35               goldnaxe only     26
    altbeast only      3               None in goldnaxe  16

The 26 are real patterns that altbeast was never back-filled with, and the
16 Nones mean "this title does not have the idiom" -- which patch_game.py's
T() could not distinguish from a derivation gap, because nothing said which
keys may legally be absent. This file says it.

WHAT A SCHEMA ENTRY IS NOT. It is not the pattern write-up and it is not the
detector. It is the contract: name, layer, shape, whether absence is legal,
and where the evidence lives. The write-up and the detector belong in the
layer package next to it, so that the thing that explains a pattern and the
thing that finds it in a new ROM cannot drift apart.

ADDING A KEY. Add it here first, with `evidence` naming the log entry that
proved it. A key that is not in this table is rejected by check(), which is
how a second title stops inventing private vocabulary.
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

LAYERS = {
    'cpu68k':   'true of any 68000 program, any board, any vendor',
    'decomp':   'true of any 68K program read through a disassembler or decompiler',
    's16b':     'true of the Sega System 16B board',
    'segaprog': "true of Sega's 68K arcade program lineage",
    'port32x':  'exists only because the 32X port needs it',
}


class Key:
    """One pattern's contract.

    name      the TABLES key
    layer     one of LAYERS
    shape     'int' | 'tuple' | 'list' | 'dict' | 'list[int]' | 'list[tuple]' |
              'list[dict]' -- checked loosely; the point is to catch a key that
              changed KIND between titles, not to type-check its contents
    nullable  True iff None is a legal value meaning "this title has no such
              idiom". False means the idiom is universal on this board and a
              None is a derivation gap.
    required  True iff every game file must carry the key (present, possibly
              None when nullable)
    what      one line: what the value means
    evidence  where the pattern was established
    """

    __slots__ = ('name', 'layer', 'shape', 'nullable', 'required', 'what', 'evidence')

    def __init__(self, name, layer, shape, what, evidence,
                 nullable=False, required=False):
        assert layer in LAYERS, f'{name}: unknown layer {layer!r}'
        self.name, self.layer, self.shape = name, layer, shape
        self.nullable, self.required = nullable, required
        self.what, self.evidence = what, evidence

    def fits(self, v):
        """Loose shape check. Returns None if the value fits, else a reason."""
        if v is None:
            return None if self.nullable else 'None, but the key is not nullable'
        base = self.shape.split('[')[0]
        want = {'int': int, 'tuple': tuple, 'list': list, 'dict': dict, 'str': str}[base]
        if base == 'int' and isinstance(v, bool):
            return 'bool where int expected'
        if not isinstance(v, want):
            return f'{type(v).__name__} where {base} expected'
        if '[' in self.shape and isinstance(v, list) and v:
            inner = self.shape[self.shape.index('[') + 1:-1]
            iw = {'int': int, 'tuple': tuple, 'dict': dict, 'list': list}[inner]
            if not isinstance(v[0], iw):
                return f'list of {type(v[0]).__name__} where list of {inner} expected'
        return None


def _k(*a, **kw):
    return Key(*a, **kw)


# ---------------------------------------------------------------------------
# THE CATALOGUE. Required = patch_game.py reads it through T() today.
# ---------------------------------------------------------------------------
KEYS = [

    # --- cpu68k -------------------------------------------------------------
    _k('TAS_SITES', 'cpu68k', 'list[tuple]',
       'every `tas` site: (offset, expected 4 bytes, shim thunk). The Mega '
       "Drive drops TAS's read-modify-write cycle, so each one needs a thunk "
       'per addressing form.',
       'LOOP-DECOMPILE-GOLDNAXE 9; altbeast 5 sites, goldnaxe 16 in reachable code',
       required=True),
    _k('TAS_SITES_DUP', 'cpu68k', 'list[int]',
       'tas sites in a program region no trace reaches (duplicate/residue code)',
       'LOOP-DECOMPILE-GOLDNAXE 9'),
    _k('ABSW_JMP', 'cpu68k', 'tuple',
       'the one abs.w control transfer into code: (site, target). abs.w sign-'
       'extends, so it cannot hold a relocated 0x9xxxxx target.',
       'TOOLKIT "MD-hardware landmines"; altbeast 0x1B5C6, goldnaxe 0x5F52',
       nullable=True, required=True),
    _k('LOW_VECTOR_READS', 'cpu68k', 'list[int]',
       'sites that read the exception-vector area as DATA (lea 0x0 / addal 0x0); '
       'in the port address 0 belongs to the host, so each must be redirected',
       'LOOP-DECOMPILE-GOLDNAXE 10; altbeast 4 sites, goldnaxe 6',
       required=True),

    # --- decomp -------------------------------------------------------------
    _k('REBASE_TABLES', 'decomp', 'list[tuple]',
       'jump tables of absolute code pointers: (start, count[, rule]). Bound '
       "each by the consumer's own andi/cmpi where it has one, by adjacency "
       'where the run reaches the next table, else by the pointer run (a '
       'filter, and therefore a hypothesis).',
       'LOOP-DECOMPILE 17 (the filter that merged two tables); '
       'LOOP-DECOMPILE-GOLDNAXE 10; tools/s16b_jumptables.py',
       required=True),
    _k('REBASE_EXCLUDE', 'decomp', 'list[tuple]',
       'regions no relocation pass may touch: word tables whose contents forge '
       'valid-looking pointers',
       'TOOLKIT "Hard-won invariants"', required=True),
    _k('STRIDE_TABLES', 'decomp', 'list[tuple]',
       'asset tables with embedded ROM pointers: (start, count, stride, ptr offset)',
       'altbeast 0x1CE2 tilemap RLE per-round table', nullable=True, required=True),
    _k('IMM_OVERRIDES', 'decomp', 'list[tuple]',
       '#imm values that ARE pointers despite landing in a data register',
       'altbeast 0x3C92 sprite frame-table base', nullable=True, required=True),
    _k('DATA_EXCLUDE', 'decomp', 'list[tuple]',
       'raw-data regions the operand scan must not treat as code. Data that '
       'decodes as a plausible hardware operand is the classic false positive.',
       "altbeast's spawn script looked like tile-RAM operands to objdump and "
       'patching it corrupted a spawn', required=True),
    _k('DATA_PTR_NORM', 'decomp', 'list[tuple]',
       'stored data-pointer readers normalised at use time: (site, bytes, thunk)',
       'altbeast spawn walker 0xDBA8, palette streamer 0x30D0',
       nullable=True, required=True),
    _k('DISPATCHERS', 'decomp', 'list[tuple]',
       'handler-pointer call funnels: (site, expected bytes, thunk). A pointer '
       'read out of an object field is a 68K address and cannot be called '
       'directly once the program is relocated.',
       'tools/cport_dispatch.py; LOOP-DECOMPILE 28; goldnaxe 6 funnels',
       required=True),
    _k('DISPATCHERS_DUP', 'decomp', 'list[int]',
       'dispatch funnels in unreached duplicate code',
       'LOOP-DECOMPILE-GOLDNAXE 10'),
    _k('HARVESTED_HANDLERS', 'decomp', 'list[int]',
       'handler values observed live in the object tables. Pointers that live '
       'only in data records are invisible to a static scan; harvest them from '
       'a running machine.',
       'TOOLKIT "Roadmap"; tools/s16b_handler_harvest.lua; goldnaxe 37 from the arcade',
       required=True),
    _k('HARVEST_BLACKLIST', 'decomp', 'list[int]',
       'harvested values that also occur as ordinary data and must not be rebased',
       'altbeast vector-table values', required=True),
    _k('HARVEST_BOUND', 'decomp', 'int',
       'occurrences are rebased only below this offset (packed assets live above)',
       'altbeast 0x28000, goldnaxe 0x40000', required=True),

    # --- s16b ---------------------------------------------------------------
    _k('MEMMAP', 's16b', 'dict',
       'the 68K memory map this title runs with, captured from the 315-5195 '
       'register writes on a RUNNING machine. NOT read from a ROM: Golden Axe '
       "carries Altered Beast's table byte-identically at MCU 0xFEA and never "
       'uses it.',
       'LOOP-DECOMPILE-GOLDNAXE 2; tools/s16b_map_probe.lua'),
    _k('SPRITE_BASE_VAR', 's16b', 'int',
       'work-RAM long holding the sprite-RAM base when the board moves it. '
       "Golden Axe's MCU remaps region 4 every vblank and publishes the page "
       'here, so sprite writes are register-indirect and need no patch sites.',
       'LOOP-DECOMPILE-GOLDNAXE 3'),
    _k('MCU_BUSY', 's16b', 'int', 'MCU screen-sync busy mailbox',
       'NOTES "i8751 MCU"; per-title, never copied between titles'),
    _k('MCU_COINS', 's16b', 'int', 'mailbox where the MCU posts coin/service',
       'altbeastj took no coins until this was derived rather than copied'),
    _k('MCU_SND', 's16b', 'int',
       'sound-command mailbox the MCU forwards to the Z80 latch (0xFF = idle)',
       'LOOP-DECOMPILE-GOLDNAXE 6, proven by trace after three taps under-reported it'),
    _k('MCU_SIGNATURE', 's16b', 'tuple',
       '(address, words) the conductor must rewrite every vblank after the game '
       'clears them. A shim that only raises IRQ4 hangs in the frame wait.',
       'LOOP-DECOMPILE-GOLDNAXE 6'),
    _k('MCU_ROM_CHECK', 's16b', 'tuple',
       'program-ROM range the MCU checksums before releasing the 68K from reset',
       'LOOP-DECOMPILE-GOLDNAXE 3'),
    _k('LAYER_REG_SITES', 's16b', 'list[tuple]',
       'writers of the tile-layer registers in text RAM (+0xE80 page select, '
       '+0xE90 vpos, +0xE98 hpos)',
       'LOOP-DECOMPILE 11 and jts16_mmr.v:94-105'),
    _k('PAGE_REG_PTRS', 's16b', 'tuple',
       'work-RAM longs holding POINTERS to the page-select registers. Where the '
       'game writes through them, retarget the pointers at boot instead of '
       'patching the stores.',
       'LOOP-DECOMPILE-GOLDNAXE 12'),
    _k('PAGE_SHADOWS', 's16b', 'tuple', 'work-RAM shadows of the two page-select words',
       'LOOP-DECOMPILE-GOLDNAXE 12'),
    _k('TILE_PRIORITY_PAGES', 's16b', 'list[int]',
       'tile pages whose words carry the priority bit',
       'LOOP-DECOMPILE-GOLDNAXE 12, measured on the arcade'),

    # --- segaprog -----------------------------------------------------------
    _k('FRAME_FLAG', 'segaprog', 'int',
       'byte the vblank handler sets and the frame wait spins on',
       'LOOP-DECOMPILE-GOLDNAXE 6'),
    _k('FRAME_SKIP_FLAG', 'segaprog', 'int',
       'byte that makes the vblank handler skip the whole frame side',
       'LOOP-DECOMPILE-GOLDNAXE 6'),
    _k('MISSED_FRAME_CTR', 'segaprog', 'int',
       "the game's own missed-frame counter, incremented when it had not "
       'consumed the previous flag',
       'LOOP-DECOMPILE-GOLDNAXE 6; altbeast 0xFFF144'),
    _k('ATTRACT_FLAG', 'segaprog', 'tuple',
       '(address, bit) set during attract and cleared at START. Measure it on '
       "the arcade: altbeast's first reading was INVERTED and cost a regression.",
       'LOOP-DECOMPILE-GOLDNAXE 12'),
    _k('CREDITED_FLAGS', 'segaprog', 'tuple',
       'per-player in-game flags the HUD writers select on',
       'LOOP-DECOMPILE-GOLDNAXE 12'),
    _k('SCENE_BYTE_CANDIDATES', 'segaprog', 'list[int]',
       'HYPOTHESIS list: work-RAM bytes that change across attract/coin/start/'
       'play and might index the scene. Not a derived key until a consumer is read.',
       'LOOP-DECOMPILE-GOLDNAXE 12'),
    _k('SCROLL_SHADOWS', 'segaprog', 'dict',
       'the camera-to-register engine: shadow addresses and the routine that '
       'derives page selects from the camera. Where this exists there is no '
       'scene byte behind the page selects.',
       'LOOP-DECOMPILE-GOLDNAXE 12b'),
    _k('RLE_EVEN_PASS', 'segaprog', 'int',
       'head of the tilemap RLE loader even-byte pass',
       'altbeast 0x16CC, goldnaxe 0x200A; the instruction layout differs per title',
       required=True),
    _k('RLE_ODD_PASS', 'segaprog', 'int',
       'head of the odd-byte pass where it is a separate loop',
       'LOOP-DECOMPILE-GOLDNAXE 9'),
    _k('STRIP_BLITTERS', 'segaprog', 'list[tuple]',
       'block copies whose tile offset is the first word of the table at aN: '
       '(site, opcode, base, reg). Page is only knowable at run time.',
       'LOOP-DECOMPILE-GOLDNAXE 9; goldnaxe 8 sites'),
    _k('STRIP_BLITTER_BASE', 'segaprog', 'int',
       "altbeast's single strip-blitter base load (superseded by STRIP_BLITTERS)",
       'LOOP 23', nullable=True, required=True),
    _k('STRIP_BLITTER_MOVEW', 'segaprog', 'int',
       'the movew that clobbers the patched base in that idiom',
       'LOOP 23', nullable=True, required=True),
    _k('SPAWN_META', 'segaprog', 'tuple',
       'per-round meta-table of spawn-script pointers',
       'altbeast 0x1D32A', nullable=True, required=True),
    _k('SPAWN_LO', 'segaprog', 'int', 'low bound of the spawn-script region',
       'altbeast 0x1D300', nullable=True, required=True),
    _k('SPAWN_CAP', 'segaprog', 'int', 'walk cap for the spawn-script scan',
       'altbeast 0x1E000', nullable=True, required=True),
    _k('TEXT_IDIOM', 'segaprog', 'list[tuple]',
       'text-writer movew->addw rewrites: (offset, old word, new word)',
       'LOOP 23', nullable=True, required=True),
    _k('TXT_LOOP_HEADS', 'segaprog', 'dict',
       'the shared text copy/clear loop heads every text writer enters, the '
       'clear being a read-modify-write site',
       'altbeast 0x3A9A/0x3AA4, goldnaxe 0x3EB0/0x3EBA'),
    _k('TXT_WRAM_WRITERS', 'segaprog', 'list[dict]',
       'text writers staged in the WRAM mirror: credit line and HUD',
       'LOOP 27 q4'),
    _k('TXT_WRAM_CLEAR_SITES', 'segaprog', 'list[int]',
       'scene-level text FILL entries a pending footprint copy must not re-plant over',
       'the phantom P2 orbs, 2026-09-07', nullable=True),
    _k('BOOT_JUMPINS', 'segaprog', 'list[tuple]',
       'control transfers INTO a boot region displaced into RAM',
       'altbeast 0x400-0x807', nullable=True, required=True),
    _k('BOOT_PCREL', 'segaprog', 'list[tuple]',
       'pc-relative to absolute fixups inside that displaced boot copy',
       'altbeast', nullable=True, required=True),

    # --- port32x ------------------------------------------------------------
    _k('PAL_DIRTY_SITES', 'port32x', 'list[tuple]',
       'palette writers to mark dirty: (site, displaced length, region mask, note). '
       'Masks are the static footprint read from the loop AFTER each site.',
       'LOOP 22; altbeast 42 sites, goldnaxe 53', required=True),
    _k('PAL_DIRTY_SITES_DUP', 'port32x', 'list[int]',
       'palette writers in unreached duplicate code', 'LOOP-DECOMPILE-GOLDNAXE 8'),
    _k('PAL_THUNK_A', 'port32x', 'int',
       'a colour-cycle engine whose write offset is only known at run time, so '
       'the mask must be derived in the thunk',
       'altbeast 0x30C2', nullable=True, required=True),
    _k('PAL_THUNK_APOST', 'port32x', 'int',
       'the mark-AFTER site for that engine. Marking BEFORE the stores ships the '
       'old colours and clears the bit, leaving the mirror one step behind forever.',
       'LOOP29 165-166', nullable=True, required=True),
    _k('PAL_THUNK_B', 'port32x', 'list[int]',
       'queued-pointer palette writers (moveal (a2)+,a1; moveal (a2)+,a0); the '
       'region comes from a1 at write time',
       'altbeast 0x2DC8/0x3C5A, goldnaxe 0x328C', required=True),
    _k('PAL_PTR_USE_SITES', 'port32x', 'list[tuple]',
       'a palette POINTER stored into an object field: mark at USE, not at the '
       'store of the pointer',
       'LOOP29 166; goldnaxe 0x55A0'),
    _k('PAL_LAUNCH', 'port32x', 'tuple',
       'palette-cycle launch table entries the harvest missed',
       'altbeast', nullable=True, required=True),
    _k('TILE_DIRTY_SITES', 'port32x', 'list[tuple]',
       'tile-RAM writer roots: (site, opcode word, target, page bits). Page bits '
       'are read from each loop bound, not guessed.',
       'LOOP 3c; altbeast 25 sites, goldnaxe 24', required=True),
    _k('TILE_DIRTY_SITES_DUP', 'port32x', 'list[int]',
       'tile writers in unreached duplicate code', 'LOOP-DECOMPILE-GOLDNAXE 9'),
    _k('TILE_PTR_USE_SITES', 'port32x', 'list[tuple]',
       'a tile address computed into an object field: mark at use',
       'LOOP-DECOMPILE-GOLDNAXE 9'),
    _k('TILE_READONLY_LEAS', 'port32x', 'list[int]',
       'tile-RAM leas that only READ: remapped, never thunked',
       'LOOP-DECOMPILE-GOLDNAXE 9'),
    _k('FMGATE_ENTRIES', 'port32x', 'list[tuple]',
       'FM-gate entries: (site, displaced length, expected first word, note)',
       'LOOP 23', nullable=True, required=True),
    _k('FMGATE_SPANS', 'port32x', 'list[tuple]',
       'rts-bounded regions around every framebuffer-destined writer',
       'LOOP 23; tools/s16b_fmgate_spans.py', required=True),
    _k('FMGATE_ENTRIES_BY_SPAN', 'port32x', 'dict',
       'control transfers into each span, before the gate thunk shape is fixed',
       'LOOP-DECOMPILE-GOLDNAXE 10'),
    _k('WRAM_BLOCKS', 'port32x', 'list[tuple]',
       'work-RAM blocks the port relocates or mirrors', 'altbeast'),
    _k('WRAM_BLOCKS2', 'port32x', 'list[tuple]',
       'second-stage work-RAM blocks', 'altbeast'),
    _k('WRAM_ENTRIES', 'port32x', 'list[int]',
       'entry points into the relocated work-RAM code', 'altbeast'),

    # --- port32x: THE GAME'S OWN WORK-RAM CELLS the port reads (2026-10-29) --
    # 68K addresses. tools/emit_game_cfg.py emits each as GAME_<KEY> in the generated
    # sh_src/game_cfg.h, the one place the engine C learns where a title keeps them.
    _k('WRAM_PALETTE', 'port32x', 'int',
       "the game's live palette RAM mirror, 128 colour sets x 16 words",
       'memory game-palette-is-wram (2026-09-02); md_main.c:2886', nullable=True),
    _k('WRAM_ROUND', 'port32x', 'int', 'current round, 0-based (& 7)',
       'md_main.c:4158; PORTROUND', nullable=True),
    _k('WRAM_MODE', 'port32x', 'int',
       'game mode flags: bit 0 = credit game vs attract (LESSONS.md:11360 reads set = '
       'credit game; md_main.c:4153 reads clear = credited -- unresolved)',
       'md_main.c:4159', nullable=True),
    _k('WRAM_P1_STATE', 'port32x', 'int',
       'player 1 state byte; (P1|P2) & 1 = a game or demo running', 'md_main.c:4156',
       nullable=True),
    _k('WRAM_P2_STATE', 'port32x', 'int', 'player 2 state byte', 'md_main.c:4156',
       nullable=True),
    _k('WRAM_CUTSCENE', 'port32x', 'int', 'non-zero while a cut scene plays',
       'md_main.c:4157', nullable=True),
    _k('WRAM_ATTRACT_STEP', 'port32x', 'int', 'attract-sequence step (>> 2 = picture)',
       'md_main.c:4160, r60_planes.c:881', nullable=True),
    _k('WRAM_FRAME_LATCH', 'port32x', 'int',
       "the game's frame latch, clear while its frame body runs",
       'md_main.c:4079; memory scene-timer-alignment', nullable=True),
    _k('WRAM_TILE_BANKREQ', 'port32x', 'int',
       'tile bank request byte the game writes for the MCU (bank in bits 0-2)',
       'md_main.c:182, r60_tiles.h:10', nullable=True),
    # --- port32x: THE SECRET MENU's per-title content (hold B at boot; testcart.c:2340) --
    _k('TITLE', 'port32x', 'str', "the title's name on the port's own screens",
       'testcart.c:2354', nullable=True),
    _k('ROUNDS', 'port32x', 'int', 'how many rounds a starting-round option offers',
       'testcart.c:2374, port_l2.c port_frontend_settings', nullable=True),
    _k('DSW2_DEFAULT', 'port32x', 'int', 'the arcade DIP switch bank 2 the port boots with',
       'memory arcade-oracle-dips (66/54); port_l2.c port_frontend_settings', nullable=True),
    _k('DSW2_OPTION_MASK', 'port32x', 'int', 'DSW2 bits the menu options replace',
       'port_l2.c port_frontend_settings', nullable=True),
    _k('MENU_DIFFICULTY', 'port32x', 'list[tuple]',
       'difficulty choices, (label, DSW2 bits); index 0 is ARCADE (keep the DIP)',
       'testcart.c:2343, port_l2.c port_frontend_settings', nullable=True),
    _k('MENU_LIVES', 'port32x', 'list[tuple]', 'lives choices, (label, DSW2 bits)',
       'testcart.c:2344, port_l2.c port_frontend_settings', nullable=True),
    _k('WRAM_SPR_ORDER', 'port32x', 'int',
       'sprite draw-order lists, 256 bytes, 0xFF = empty', 'port_l2.c:2729',
       nullable=True),
]

BY_NAME = {k.name: k for k in KEYS}
REQUIRED = [k.name for k in KEYS if k.required]

assert len(BY_NAME) == len(KEYS), 'duplicate key name in KEYS'


# ---------------------------------------------------------------------------
# validation
# ---------------------------------------------------------------------------
def load_game(game):
    """Import games/<game>/game.py and return its TABLES dict."""
    p = ROOT / 'games' / game / 'game.py'
    if not p.exists():
        raise FileNotFoundError(p)
    spec = importlib.util.spec_from_file_location(f'game_{game}', p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return getattr(m, 'TABLES', {})


def validate(tables, game='?'):
    """Return (errors, warnings) for one game's TABLES against the schema."""
    errors, warnings = [], []
    for name in sorted(set(tables) - set(BY_NAME)):
        errors.append(f'{game}: {name} is not in the schema — add it to '
                      'kit/schema.py KEYS with its evidence, or delete it')
    for k in KEYS:
        if k.name not in tables:
            msg = (f'{game}: missing {k.name} ({k.layer}) — {k.what.splitlines()[0]}')
            (errors if k.required else warnings).append(msg)
            continue
        why = k.fits(tables[k.name])
        if why:
            errors.append(f'{game}: {k.name} is {why}  (schema says {k.shape}'
                          f'{", nullable" if k.nullable else ""})')
    return errors, warnings


def check(games=None):
    """Audit every game file. Returns the number of errors."""
    if not games:
        games = sorted(p.parent.name for p in (ROOT / 'games').glob('*/game.py'))
    total = 0
    for g in games:
        try:
            tables = load_game(g)
        except Exception as e:                      # noqa: BLE001 - report, don't crash
            print(f'{g}: could not load ({e})')
            total += 1
            continue
        errors, warnings = validate(tables, g)
        total += len(errors)
        covered = sum(1 for k in KEYS if k.name in tables)
        print(f'== {g}: {covered}/{len(KEYS)} keys, {len(errors)} errors, '
              f'{len(warnings)} not-yet-derived')
        for e in errors:
            print(f'   ERROR   {e}')
        for w in warnings:
            print(f'   {w.split(": ", 1)[1]}')
    return total


def catalogue():
    for layer, blurb in LAYERS.items():
        ks = [k for k in KEYS if k.layer == layer]
        print(f'\n== {layer} ({len(ks)} patterns) — {blurb}')
        for k in ks:
            flags = ('required' if k.required else 'optional') + (', nullable' if k.nullable else '')
            print(f'   {k.name:24} {k.shape:12} [{flags}]')
            print(f'      {k.what}')
            print(f'      evidence: {k.evidence}')


def index_md():
    """kit/INDEX.md, generated. Never hand-edit."""
    out = ['# kit — pattern index',
           '',
           'GENERATED by `python3 -m kit.schema index`. Do not hand-edit.',
           '',
           f'{len(KEYS)} patterns across {len(LAYERS)} layers. '
           'Each is a key of `tools/game_<name>.py` TABLES; the game file is '
           'that title\'s instantiation.', '']
    for layer, blurb in LAYERS.items():
        ks = [k for k in KEYS if k.layer == layer]
        out += [f'## {layer} ({len(ks)})', '', f'_{blurb}_', '',
                '| key | shape | absence | what | evidence |',
                '|---|---|---|---|---|']
        for k in ks:
            absence = 'may be None' if k.nullable else ('required' if k.required else 'optional')
            what = ' '.join(k.what.split())
            out.append(f'| `{k.name}` | {k.shape} | {absence} | {what} | {k.evidence} |')
        out.append('')
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'catalogue'
    if cmd == 'check':
        sys.exit(1 if check(sys.argv[2:]) else 0)
    elif cmd == 'index':
        (ROOT / 'kit' / 'INDEX.md').write_text(index_md())
        print(f'wrote kit/INDEX.md ({len(KEYS)} patterns)')
    else:
        catalogue()
