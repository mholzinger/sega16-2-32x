# Working-set page lifecycle

This document describes the mapper-page protocol used by the accepted 32X
renderer line. The protocol is engine infrastructure: a new Sega16 port should
reuse it and supply only its game-specific working-set selector and baked data.

## Portability boundary

| Responsibility | Owner |
| --- | --- |
| Decide whether the current scene needs the extra sprite working set | Game adapter, currently `r60_game_workingset()` |
| Generate AB1/ZB1 data and bounds for the mapped page | Per-game graphics build |
| Arbitrate mapper requests against sound commands | Engine, `r60_comm_arbiter.h` |
| Reserve a renderer-safe mapping window | Engine, `r60_workingset.h` |
| Post and acknowledge the 68K mapper command | Engine plus `md_bank.c` |
| Publish the new page generation | Engine, master SH-2 |
| Purge page-derived caches before drawing | Engine, slave SH-2 |

Altered Beast currently selects its working set from native game RAM and bakes
the Neff poses named by `tools/build_line.sh`. Neither rule belongs in the
generic reservation or cache-coherency protocol.

## State and ownership

| State | Writer | Meaning |
| --- | --- | --- |
| `R60_PAGE_CURRENT` | Master | Mapper page whose contents are committed for renderer use |
| `R60_PAGE_PENDING` | Master | Tentative reservation, then posted mapper request; zero means neither |
| `r60_page_gen` | Master | Generation incremented after a mapping acknowledgement |
| `r60_page_seen` | Slave | Last generation whose page-derived caches were invalidated |
| `R60_GO` / `R60_DONE` | Slave protocol | Whether a framebuffer pass is active |
| `R60_SNAPSEQ` / `R60_DRAWSEQ` | Master/slave protocol | Whether an unread snapshot still references the current page |
| `MARS_SYS_COMM10` | Master and 68K | Shared mapper/sound command mailbox |

The SH-2 control words are accessed through the uncached alias. Compiler
barriers order those transactions; this is a platform protocol, not a claim of
C11 atomic behavior.

## Mapping transition

1. The game adapter returns the desired page from `r60_page_want()`.
2. The communication arbiter chooses mapping, audio, or a bounded hold. Mapping
   cannot overwrite unrelated mailbox traffic.
3. `r60_ws_reserve_page()` tentatively writes `R60_PAGE_PENDING`, then verifies
   that the slave is between passes, no snapshot is unread, and COMM10 is free.
   A failed verification rolls the pending value back to zero.
4. The RAM-resident service routine posts the `0x5Bxx` mapper command. It must
   return through RAM because the 68K can immediately make the old cart mapping
   inaccessible.
5. While the request is pending, the master withholds new snapshots. Existing
   sound commands are neither discarded nor mistaken for mapper completion.
6. After the 68K acknowledgement, the master publishes the new current page,
   increments `r60_page_gen`, and clears `R60_PAGE_PENDING` in that order.
7. At the next pass start, the slave observes the new generation, purges the
   SH-2 cache, and clears every per-sprite baked-index token before reading the
   mapped art.
8. The slave claims the snapshot/page lease and keeps that page stable until it
   publishes `R60_DONE`.

## Planner return value

`r60_ws_plan()` returns one machine word with two fields:

| Bits | Meaning |
| --- | --- |
| upper 16 | COMM10 command to post, or zero |
| bit 0 | hold snapshot publication for this scheduler visit |

Only `r60_page_service()` writes the returned command to COMM10. An audio
command is dequeued only after that store succeeds. This keeps policy code away
from the mapper-sensitive final store and prevents idle retries from consuming
sound twice.

## Required invariants

- Never remap while `R60_DONE != R60_GO`.
- Never remap while `R60_DRAWSEQ != R60_SNAPSEQ`.
- `R60_PAGE_PENDING` may be tentative only inside the reserve-to-post sequence;
  if retained across scheduler calls, it represents an actually posted request.
- Publish current page, then generation, then clear pending.
- The slave must invalidate caches after the generation change and before any
  mapped-source or prepared-packet read.
- A cached AB1/ZB1 token is valid only for the generation in which it was
  admitted; no mapped pointer survives a generation change.
- COMM10 audio and mapping traffic share one arbiter. Neither producer may
  clear, overwrite, or silently consume the other's work.

## Verification

The host diagnostics cover protocol interleavings, scheduler order, mailbox
fairness, admission bounds and source dispatch:

- `tests/diagnostics/check_workingset_runtime.py`
- `tests/diagnostics/check_workingset_arbiter_integration.py`
- `tests/diagnostics/check_workingset_dispatch.py`

The final acceptance gate is still a full `tools/build_line.sh` rebuild and an
exact ROM comparison outside the build-stamp range.
