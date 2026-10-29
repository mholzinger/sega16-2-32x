"""kit — the game-agnostic pattern library for porting 68000 arcade programs.

Four layers, by how general each pattern is:

    cpu68k    true of any 68000 program, any board, any vendor
    decomp    true of any 68K program read through a disassembler/decompiler
    s16b      true of the Sega System 16B board
    segaprog  true of Sega's 68K arcade program lineage
    port32x   exists only because the 32X port needs it

Nothing in here may import from a game: per-title offsets live in
tools/game_<name>.py and are described, not stored, by kit.schema.
No Sega-derived bytes: evidence is cited by address and by file:line.
"""
