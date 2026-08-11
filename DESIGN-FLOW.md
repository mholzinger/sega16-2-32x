# Design flow — System 16 on the 32X, from first design to current

Three diagrams: the original software-compose design, what measurement
did to it, and the two-plane architecture running today. Companion to
ARCHITECTURE.md (facts and numbers live there); this is the shape of
the decisions.

## 1. Original design — the SH-2s ARE the video chip

The arcade board is four chips working per-scanline; the port's first
architecture replaced all of the video silicon with software on the two
SH-2s, using MAME's segaic16 source as the behavioural reference and
jtcores RTL as the hardware spec.

```mermaid
flowchart TD
    subgraph ARCADE["System 16B arcade board"]
        A68K["68000 game code"]
        TILE["Tilemap chip: BG + FG planes, text, rowscroll, alt set"]
        SPR["Sprite chip: zoom, priority"]
        MIX["Colour mixer: 128 sets x 8 pens, shadow"]
    end

    subgraph PORT["Port, original design"]
        PATCH["patch_game.py: rebase video and IO writes into the 32X map - no protection involved"]
        MD68K["MD 68000 runs the patched game"]
        STAGE["FB staging + DREQ packet: tilemap shadow, sprite list, palette, regs to SDRAM"]
        MASTER["SH-2 master: window handler, band queue, blit"]
        SLAVE["SH-2 slave: concurrent compose half"]
        SBUF["sbuf 336x240 indexed - BG opaque, FG cat0, sprites, FG cat1, text"]
        CRAM32["32X CRAM: 32 groups x 8, runtime set-to-group allocator"]
        FB["32X framebuffer, 3-window pipeline: 1 compose + 2 blit per vint chain"]
    end

    A68K -->|"same binary"| PATCH --> MD68K
    TILE -.->|"reimplemented in software"| SBUF
    SPR -.->|"reimplemented in software"| SBUF
    MIX -.->|"reimplemented"| CRAM32
    MD68K --> STAGE --> MASTER
    MASTER <--> SLAVE
    MASTER --> SBUF --> FB
    FB --> OUT["Display: 20 Hz, full S16 fidelity"]

    subgraph GATES["Truth sources"]
        MAME["MAME parity harness - verification rig"]
        JT["jtcores RTL - hardware spec"]
        ARES["ares + Mike's play pass - hardware truth"]
    end
    OUT --- GATES
```

## 2. What measurement did to that design

Every box below is a measured result, not an opinion; the dead ends are
recorded with their killing numbers in LOOP*.md NEGATIVE RESULTS.

```mermaid
flowchart TD
    START["Software compose ships at 20 Hz with green tearing"]
    FLOOR["MEASURED: blit is bus-bound - 13.6 cyc/longword, write buffer buys nothing, 28.5 of 38 vblank lines. Framerate is a wall, not a bug"]
    TEAR["MEASURED: blit skips 23.4% of cycles = the green tearing"]

    START --> FLOOR
    START --> TEAR

    subgraph DEAD["Dead ends - do not re-attempt"]
        IDLE["Chaotix idle-token: premise is a parked SH-2, ours is saturated - cadence 3.03 to 7.48"]
        CMDINT["Interrupt pickup: deferrals 4.6x, skips 66% - ISR cost alone 3.1 points"]
        DIRTY["Dirty-row blit: only 31.8% transparent vs 25% break-even"]
        DMAC["DMAC blit: 1.77x SLOWER on ares"]
    end
    FLOOR --> DEAD

    PIVOT["PIVOT DECISION: stop redrawing what dedicated silicon can draw - move the tile planes onto the MD VDP, keep sprites + priority tiles on the SH-2s. Evidence: the commercial library did it this way"]
    TEAR --> PIVOT
    DEAD --> PIVOT

    subgraph SLICES["Pivot slices, each verified before the next"]
        S1A["1a: MD video composites through the 32X layer - through bit = CRAM entry bit 15, was never armed"]
        S1B["1b: real tiles reach MD VRAM via a self-describing packet in dead FB space - idempotence is load-bearing"]
        S1C["1c: name tables from the real tilemap, per-band alt-set and rowscroll selection, hardware scroll"]
    end
    PIVOT --> S1A --> S1B --> S1C

    subgraph BUGS["The archaeology 1c uncovered"]
        PALSH["Tag arrays placed inside PAL_SH - palette stream clobbered them every batch"]
        MSHORT["-mshort: constant-only VDP commands shifted left 16 evaluate to ZERO - silent null commands"]
        EVICT["First-come residency dies on the title: 1120 cycling codes vs 1024 slots - LRU eviction required"]
        VOID["The 'void' was never a render bug: the graveyard wall and trees are the FG LAYER, and removing the slave's software compose removed FG cat-0's last renderer"]
        SEQ["Instrument lessons: fake videoram space, dying write taps, timeline drift across builds - anchor by game state, trust the WIP-commit clock"]
    end
    S1C --> BUGS

    PAYOFF["MEASURED PAYOFF: blit skips 23.4% to 0.2% - tearing killed. Framerate unchanged - known and accepted. Cost: receiver spans 140 scanlines, ares cadence 4.64 vints/cycle"]
    BUGS --> PAYOFF
```

## 3. Current design — two-plane hybrid

The MD VDP draws what it is good at (two scroll planes); the SH-2s draw
what only they can (zoomed sprites, priority tiles); the 32X composite
stitches them per pixel via the through bit.

```mermaid
flowchart TD
    MD68K["MD 68000: patched game + shim vint handler"]

    subgraph SH2["SH-2 side, per window"]
        WALK["Name-table walk both layers: per-band alt-set + rowscroll, code and set decode per segaic16"]
        ALLOC["MD residency: md_tag 1024 slots keyed code+set+plane, LRU evict, reserved blank 1023"]
        PENS["Palette pack: 3 MD lines, usage-mask pens - only pixels resident tiles actually use claim pens; FG pixel 0 never does. Drift tolerated, never invalidates"]
        SHIP["Tile shipper: ROM pixels through per-set pen remap - FG variant keeps pixel 0 transparent. Demand-biased batches"]
        PKT["Packet in dead FB block, 9-phase: 1 tile batch + 4 Plane B chunks + 4 Plane A chunks + 48 live CRAM words + scroll"]
        COMPOSE["Compose keeps: sprites + FG cat-1 into sbuf, blit 3-window pipeline unchanged"]
    end

    subgraph MDVDP["MD VDP renders"]
        PB["Plane B = S16 BG layer"]
        PA["Plane A = S16 FG cat-0, pixel-0 transparent over B"]
        CRAM["CRAM lines 1-3 refreshed every window - fades track live"]
    end

    MD68K -->|"receiver in vint handler"| PKT
    WALK --> ALLOC --> SHIP --> PKT
    PENS --> SHIP
    PKT --> PB & PA & CRAM
    COMPOSE --> FB32["32X FB: sprites + priority tiles, pixel 0 = through"]
    PB & PA --> THRU["Per-pixel composite: 32X in front, through bit shows MD planes"]
    FB32 --> THRU --> OUT["Display: tearing gone, colours arcade-class, cadence 4.64 vints/cycle pending write budget"]

    subgraph OPEN["Open, in priority order"]
        O1["Receiver write budget or DMA: 140-line VDP span is the cadence tax - target 3 vints/cycle"]
        O2["Section 11 offline palette precompute: exact colours + cycling-aware exclusive pens - un-freezes shimmer, retires runtime fallbacks"]
        O3["Small artifacts: purple blocks, wall speckles, corner drips, mint tint"]
    end
    OUT --- OPEN
```

The through-line of the whole project: **every box that survived did so
by measurement** — the original design fell to numbers, not taste, and
the pivot's slices each carried their own verification before the next
was built. The toolkit deliverable (TOOLKIT.md) inherits the pieces
that are game-agnostic: the packet transport, the residency allocator,
the palette pack, the parity and capture instruments, and the
negative-results ledger that keeps the next title from re-walking this
map.
