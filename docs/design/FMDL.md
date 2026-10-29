# FMDL — the framebuffer-marshalled display list

What this port's harness actually is, what to call it, and what it costs.
Measured 2026-09-27 by the test cartridge on both emulating machines.

## The name, and which parts of it are borrowed

**Framebuffer-Marshalled Display List — FMDL.** Every word is an existing term
of art, which is the point: the mechanism is not new, only the substrate is.

**Display list.** A list of drawing commands produced by one processor and
consumed by a display processor. Old and well established in graphics
architecture -- SGI's display lists, the N64's, the PlayStation's ordering
table. The modern name for the same idea is the **command buffer** (Vulkan,
D3D12). Our 736-word packet IS a display list: sprite records, tile records and
a palette shadow, produced per frame by one CPU for another to execute.

**Marshalled.** The correct word for serialising structured data into a wire
format to cross a boundary, which is exactly what the 68K does -- it packs
records the SH-2 unpacks. Not "copied": the layout is a protocol.

**Beam-raced** describes the deadline. The assembly must finish before the
consumer needs it, every frame, and the term has been in use since the Atari
2600 (Montfort and Bogost's *Racing the Beam*).

So: a beam-raced, cross-ISA command buffer.

## What is actually unusual here

Not the pattern. Command buffers between heterogeneous processors are how every
console GPU has ever worked.

**The unusual part is the SUBSTRATE: the transport medium is the display
surface itself.** The commands travel through the pixels, time-shared by the
very same arbitration bit (FM) that governs who may touch the display. On any
machine with a mailbox, a FIFO or shared RAM you would never route commands
through the framebuffer.

And it is not a choice. On the 32X the 68K **cannot address the SH-2's SDRAM at
all**; the framebuffer is the only large window both sides can reach. The
architecture is forced by the topology.

**A claim worth NOT making:** I cannot verify that nobody has done this before.
The pattern is old, 32X homebrew is sparsely documented, and a negative about
prior art is not something this project can establish. What is defensible is
that the substrate is unusual, that it is forced here, and that the cost of it
is now measured.

## What it costs — the number that explains the whole project

736 words (688 body + 48 palette shadow, `md_main.c:1403`) through the real
packet base 0x851A00, in raster lines of a 262-line frame:

| leg | ares | MiSTer core |
|---|---|---|
| ASSEMBLE (build the list in work RAM) | 55 | 57 |
| BLAST (marshal it into the framebuffer) | 92 | 94 |
| **assemble + blast** | **147** | **151** |
| LIFT (read it back, if the 68K did it) | 68 | 71 |
| **share of a 262-line frame** | **56%** | **57%** |

**The transport alone costs 56-57% of the frame, before a single pixel is
rendered or a line of game logic runs.** Both machines agree within 3%.

## Why that is the ceiling, arithmetically

The project's own frame timeline puts the game's work at 147 lines light and
185 heavy. Add the transport:

    light   147 game + 147 transport = 294 lines   against 262 available
    heavy   185 game + 151 transport = 336 lines   against 262 available

    -> 1.12 to 1.28 vints per frame  ->  47% to 53% of 60 Hz

Which is the cadence this project has measured all along (50-70% depending on
scene). **An instrument built for something else, on a different day, derives
the observed frame rate from first principles.** That is the strongest kind of
agreement available here, because neither figure was fitted to the other.

It also says plainly where the ceiling is: no amount of shaving the game's side
gets a single-vint frame while the transport costs half the frame. The lever is
the transport's SHAPE -- fewer, denser records -- which is what TILESLIM's
2-word record was, and why the packet body being the wall (MDBATCH saturating
at 64) was the finding it was.
