# Deferred MD palette repair

The active R60 interrupt shim could queue a late background palette but never
drain it. The drain remained inside the unused legacy shim. Frozen normal06
linked code has only two references to the pending flag: writes clearing it
for a newer immediate palette and setting it for a deferred palette. There is
no active read. This establishes a source defect independently of the smoke
performance investigation.

The repair shares the existing 64-word WRAM-to-CRAM upload between both shims.
The active shim calls it before stage playback and its FM guard. Upload still
requires beam lines E0 through F7; the pending flag clears after the DMA command.
An empty queue does no VDP work. A newer palette still supersedes queued work.
Frontend restoration clears pending work before restarting interrupts, even
without TILEBATCH, so an old game palette cannot replay after the menu.

The source-derived sanitizer test covers all 256 beam values, exact upload
length/payload/source/autoincrement, an empty queue, a missing fresh packet,
FM ownership, replacement and frontend reset. The existing display-hold tests
also pass. Linked wsplay08 reaches the drain before stage playback/FM checks;
its MD payload is 22,496 bytes, 144 bytes larger, with unchanged BSS size and
all original fences intact.

Cold 1,800-frame headless samples show 118 pending sample frames and a longest
run of 20 in normal06, versus 18 and 3 in normal08. These are start-of-frame
observations from two runs, not per-window latency bounds, displayed-color
parity, smoke speed or hardware measurements.

All five real level starts and three/six-button menu reentry/residency pass.
The 65,001-frame cold health run has 64,989 VInts, 64,991 game ticks and 1,082
healthy samples, with zero timeouts or malformed records. Extended checks show
32 page switches, no pending switch, 2,602 posted/accepted sound commands,
an empty FIFO with highwater three, and no full attempts, budget yields,
dropped ticks or missed indirect calls. Graphics and audio remain exact;
linked game-code changes are same-named address relocations.

The natural GOD+tape 40,000-frame route regression uses no forced form or scene.
Against prior route04, all 440 mirror bytes and 18 fields match across 33,406
completed common native-clock keys; all 138 coherent ordered boss/level states
match. Both first observe level two at frame 36,405, score 33,800 and three
lives. Raw/in-flight differences remain in the report; this is not blanket
per-frame state, pixel or cadence equality.

The office rig's 180-second sparse capture has 24 screenshots: sample 18 is
nearly all black, followed by nonblack samples 19 through 24. This is recovery
evidence, not continuous blackout, pixel-parity or cadence proof. The wrapper
restored rdplay01 at 18:02:28; its remote SHA and selected path were verified.
The foreground-view binding hold remains open.

Frozen wsplay08 SHA256:
`6826e4d8dbbbc436cd9f3d0953acc1a5974259039ecfacee6d0c3e94157b068b`.
It is a candidate, not a new accepted alpha. Accepted rdplay01 remains the
hardware restore target.

Reproduce the source check with
`python3 tests/diagnostics/check_palette_pending.py`.
Evidence: `build/codex-working-set/palette-drain/`, including linked08,
normal06-1800 and normal08-1800; the observer semantic-ack review contains the
original linked write-only reference census.
