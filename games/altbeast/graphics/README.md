# Altered Beast working-set inputs

The title owns pose selection; the format tools own decoding and serialization.
These manifests contain descriptors only, with no graphics payload, ROM offsets
for output placement, recorded screenshots or accepted-ROM dependency:

- `neff-scaled-45.json`: exact `(bank,address,signed_pitch,flip)`, ten-bit zoom
  and maximum rendered height for each of the validated 45 scaled identities.
- `neff-unscaled-22.json`: the original 22 unscaled key/maximum-height pairs.
  Ordering is canonical; the selected identities and heights are unchanged.

The generic `tools/extend_zoom_atlas.py --empty-base` starts with the canonical
16-byte raw ZB1 header and bakes every requested identity from raw sprite words.
It fails on an invalid descriptor, duplicate identity or insufficient budget;
it does not rank or drop poses. `--base-atlas` remains the lossless extension
interface when a caller intentionally wants to preserve an existing atlas.

## Rebuild the scaled input

From the repository root, with the user's normal US ROM set available:

```sh
python3 tools/romset.py altbeast
GAME=altbeast python3 tools/gen_sprites.py

graphics_out="$PWD/build/graphics/altbeast"
mkdir -p "$graphics_out"
python3 tools/extend_zoom_atlas.py --empty-base \
  --manifest games/altbeast/graphics/neff-scaled-45.json \
  --sprites sh_src/sprites.bin --budget 62528 \
  --out "$graphics_out/neff45.zb1" \
  --report "$graphics_out/neff45.json"
```

The 1 MiB interleaved sprite input has SHA256
`a86929d1c0ab5c7735b2ce77457c074dec6d3ddfab710c12ac8629a4fd16f155`.
The rebuilt atlas is 62,528 bytes with SHA256
`4334dcb929fd0cb4a90576ef48b15d08f4d3111f9537ebf90992a476cd4fa69f`.
This is byte-identical to the validated 45-pose atlas. No 42-pose base file is read.
The unscaled manifest's canonical sorted packed-key/height mapping hashes to
`42e4b051166dd1cf4380a4eda75485347fbd1336958658a6135f26cb4e9585ab`.

```sh
python3 tests/diagnostics/check_zoom_extend.py
ALTBEAST_SPRITES=sh_src/sprites.bin \
  python3 tests/diagnostics/check_altbeast_manifests.py
```

## Normal ROM recipe

Use the existing tracked `candidate` base flags and normal working-set options.
This retains the usual release/decompile/audio input checks. Run after the
preceding atlas generation; all working-set selection inputs are explicit:

```sh
make candidate GAME=altbeast NAME=neff45-rebuilt EXTRA="\
CARTSSF=1 R60SOUND=1 R60PSGMUSIC=1 R60PTONE=1 R60SPANCAP=1536 \
R60TXTPOOLHYB=1 R60BAKEART=1 R60BAKEPAGES=1 R60PUNCHTHRU=1 \
R60SPRPREP=1 R60SPANPROGRESS=1 R60TILEBATCH=1 R60PLANROWS=1 \
R60ROWDIFF=1 R60ZOOMKEEP=1 R60BOOTBANKS=1 R60WORKSET=1 \
R60WORKSETATLAS=$graphics_out/neff45.zb1 \
R60WORKSETPOSES=$PWD/games/altbeast/graphics/neff-unscaled-22.json \
R60WORKSETCLIP=1"
```

These options leave cache/RAM-overlay/compositor/event/fixture/god/form/tape
experiments off. The page builder continues validating local AB1/ZB1 extents,
accepted global references and page/music guards. This change supplies durable
selection inputs; it does not change the runtime or Make defaults.
