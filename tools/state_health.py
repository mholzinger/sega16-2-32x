#!/usr/bin/env python3
"""Ares savestate health report (see TOOLKIT: savestate forensics).

Usage: state_health.py [path-to-.bs1]   (default rom/s16.bs1)

Locates SDRAM (probe-relocated) and MD RAM (TAS-thunk signature) in
the BST1 state and prints the pipeline health counters: build ID,
cadence (vints per k1 cycle), V-gate rejects, blit skips, deferrals,
DREQ health, dirty bitmap.
"""
import struct
import sys


def swap16(b):
    return b"".join(b[i + 1:i + 2] + b[i:i + 1] for i in range(0, len(b), 2))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "rom/s16.bs1"
    st = open(path, "rb").read()
    # STALENESS GUARD. The default path silently reads an OLD state when
    # you meant the probe rom you just launched — the readings then look
    # plausible and identical, and the only tell is the BUILD hash. Name
    # the file, and shout if a rom is newer than the state being read.
    import glob
    import os
    age = os.path.getmtime(path)
    newer = [r for r in glob.glob("rom/*.32x") if os.path.getmtime(r) > age]
    print(f"STATE: {path}")
    if newer:
        print(f"  !! WARNING: this state is OLDER than {len(newer)} rom(s) "
              f"({', '.join(os.path.basename(r) for r in sorted(newer)[:4])}"
              f"{'...' if len(newer) > 4 else ''}).")
        print("     If you meant a probe rom, pass ITS state explicitly, "
              "e.g. state_health.py rom/PROBE_spin0.bs1 — and check the "
              "BUILD line below matches the rom you ran.")
    sd = 0x23B
    sig = swap16(bytes.fromhex("4a38c02050f8c0204e75"))
    i = st.find(sig)
    if i < 0:
        print("TAS thunk signature not found — wrong/old build state?")
        return
    md = i - 0xB380

    def rd32(off):
        return struct.unpack(">I", swap16(st[sd + off:sd + off + 4]))[0]

    def rdmd16(off):
        return struct.unpack(">H", swap16(st[md + off:md + off + 2]))[0]

    vints = rdmd16(0xB0F0)
    cycles = rd32(0x28000 + 9 * 4)
    skips = rd32(0x28000 + 7 * 4)
    gates = rdmd16(0xB0FC)
    print(f"BUILD: {rd32(0x28000 + 18 * 4):08x}")
    print(f"vints={vints} cycles={cycles} -> "
          f"vints/cycle={vints / max(cycles, 1):.2f} (healthy ~3)")
    print(f"V-gate rejects={gates} ({100.0 * gates / max(vints, 1):.1f}% "
          f"of vints)")
    # PRESENTATION 2.0 (builds >= 183ce625): blits never skip — they write
    # the hidden bank at every window. DIAG[7] now counts MISSED k2 FLIPS
    # (= whole frames dropped, display keeps the last complete frame).
    # On older builds the same slot is per-band blit skips.
    print(f"flip/blit skips={skips} ({100.0 * skips / max(cycles, 1):.1f}% "
          f"of cycles; pres-2.0 builds: dropped frames)")
    # dreq_incomplete is now PER-CYCLE-RATED and split against the MD's
    # own abort counter, because the two causes need opposite fixes:
    #   aborts>0   the 68K ran out of spin budget mid-push (raise it, or
    #              shrink the packet)
    #   aborts==0  the 68K pushed everything and the DMA still did not
    #              drain (SPLIT the packet; a bigger budget is useless)
    # LOOP 7b gave aborts their own address: 0xFFB0E0. Before that it
    # shared 0xFFB0F2 with windows-completed, so EVERY abort figure read
    # from an older state is meaningless — treat pre-7b aborts as unknown,
    # not as zero.
    # LOOP 13 part 4: the MD_BG packet receive stamped its last-magic
    # diag over 0xFFB0E0 every accepted window, so on every MD_BG build
    # before the relocation push_aborts read ~0xB6B6 garbage — the
    # "aborts unchanged" alongside the 11.5% dreq_incomplete regression
    # was a clobbered counter, not a clean 68K. The magic now lives at
    # 0xFFA020; 0xFFA022 is the spin-headroom watermark (min polls LEFT
    # of the 2600 budget across completed pushes; 0xFFFF = no push yet).
    # Watermark near 0 => budget-marginal, aborts are the mechanism.
    inc = rd32(0x28000 + 17 * 4)
    aborts = rdmd16(0xB0E0)
    wmark = rdmd16(0xA022)
    if aborts in (0xB6B6, 0xB6B7) and wmark == 0:
        print("  !! push_aborts reads packet-magic garbage — pre-relocation "
              "MD_BG build; treat aborts as UNKNOWN")
    print(f"deferrals={rd32(0x28000 + 13 * 4)} "
          f"dreq_incomplete={inc} ({100.0 * inc / max(cycles, 1):.1f}% "
          f"of cycles) push_aborts={aborts} "
          f"spin_headroom_min={'n/a' if wmark == 0xFFFF else wmark}/2600")
    # LOOP 13 part 4 — DREQ residue split (0x28F80, builds with DRQR).
    # The 68K is exonerated (aborts 0, headroom 2597/2600); the residue
    # of each incomplete sprite push names the remaining mechanism:
    #   ==256  the MD pushed the 340-word TEXT layout while the master
    #          expected SPRITE/596 — wskip/prev_k PHASE DESYNC
    #   1..8   tail-drain stall: the FIFO's last groups never got DMAC
    #          service before the next window
    #   other  mid-stream stall / wild TCR
    d256, dtail, doth, dlast, dmax = (rd32(0x28F80 + i * 4) for i in range(5))
    # shared-pen drift split (builds >= 450c1ad0): small drifts stay
    # tolerated; catastrophic (d^2 >= 18) re-claims the set so wrong
    # colours (the purple walkway) self-heal within a rotation.
    dsm, dca = rd32(0x28F94), rd32(0x28F98)
    if dsm or dca:
        print(f"  pen drift: small(tolerated)={dsm} "
              f"catastrophic(re-claimed)={dca}")
    if inc and not (d256 or dtail or doth):
        print("  (dreq residue split: no data — pre-DRQR build)")
    elif d256 or dtail or doth:
        tot_r = d256 + dtail + doth
        print(f"  residue split: ==256 (phase desync)={d256} "
              f"1..8 (tail-drain)={dtail} other={doth} "
              f"last={dlast} max={dmax}")
        if d256 == tot_r:
            print("  -> ALL phase desync: fix wskip/prev_k agreement, "
                  "not the DMA")
        elif dtail == tot_r:
            print("  -> ALL tail-drain: DMAC starved at the push tail")
    # LOOP 13 magic tail (builds >= 96f2ea21): DRQR[7] counts packets
    # that landed COMPLETE (TCR 0) but word-DISPLACED — the ares FIFO
    # drops a write racing a full FIFO without counting it, and the
    # overpush backfills the length, so only the pad-word magic
    # (0xA55A5AA5 at its exact position) can see the shift. Misaligned
    # packets are skipped whole (stale beats displaced). MAME baseline:
    # 1 (a single boot-window artifact); steady growth on ares = the
    # 68K push racing a near-full FIFO -> throttle/pace the push.
    dmis = rd32(0x28F9C)
    print(f"dreq misaligned (magic-tail poisoned, skipped)={dmis}"
          f"{'' if dmis <= 1 else '  << FIFO word-loss ACTIVE'}")
    print(f"dirty bitmap now={rdmd16(0xB9FE):04X}")
    # LOOP 7c — THE STROBE. The flip/restore pair blanks the screen over
    # bank Y, which nothing composes into; it is only safe while the pair
    # fits inside vblank (38 lines). ares DEFERS an FBCTL write made
    # outside vblank to the next one, so an overrun puts empty bank Y on
    # screen for a WHOLE FRAME — the black frame. MAME cannot show this
    # (it latches immediately; 0 black frames in 150, and a forced 30-line
    # overrun still gave 0), so these counters are the only way to see it.
    # A nonzero rate here IS the strobe, and `worst` says how many lines
    # of blit have to come off to stop it.
    # DIAG[27] is in FRT TICKS, not lines: the SH-2 has no divide, so the
    # conversion moved here (~46 ticks/scanline, 38 lines of vblank = 1748).
    # PRESENTATION 2.0: the flip/restore pair is GONE — [26]/[27] read 0
    # forever (the class is extinct by construction), [28] still counts
    # blit windows, and [31] counts k2 flips that failed to latch inside
    # the vblank gate (structurally 0; nonzero = the latch model is wrong).
    late, worst, tot = (rd32(0x28000 + i * 4) for i in (26, 27, 28))
    if tot:
        print(f"restore past vblank={late}/{tot} "
              f"({100.0 * late / tot:.1f}% of blit windows) "
              f"worst={worst / 46.0:.0f} lines (vblank=38) -> "
              f"{'STROBE CONFIRMED' if late else 'strobe class dead (pres 2.0: no restore edge)'}")
    # LOOP 7i: how long the FS RESTORE took to LATCH. ares defers an FBCTL
    # write made outside vblank to the next vblank, and the master's
    # readback spin does not fail on that — it BLOCKS, with FM=1, stalling
    # the 68K too. A mean of a few ticks means latches are immediate; a
    # mean in the thousands (a frame is ~12000 ticks, a scanline ~46) means
    # the strobe and the slowness are the same bug.
    lat, latn = rd32(0x28000 + 29 * 4), rd32(0x28000 + 30 * 4)
    if tot:
        print(f"FS restore latch: mean={lat / max(tot, 1):.0f} ticks "
              f"({lat / max(tot, 1) / 46.0:.1f} lines), "
              f"{latn}/{tot} waits >1 line ({100.0 * latn / tot:.1f}%) "
              f"flip-late-latches[31]={rd32(0x28000 + 31 * 4)} "
              f"(pres 2.0 >= this build: k2 latch took >200 ticks; the "
              f"flip WAITS and restores either way — a latency signal, "
              f"not corruption. Builds cdfc4799 and earlier ABORTED the "
              f"restore on these: bank skew, real corruption — re-measure "
              f"on the fixed build.)")
    # PREEMPT-BLIT TIMEOUTS (builds >= 1152c7d1). The master's SYNC[2]
    # pickup and SYNC[5] echo waits used to be unbounded: if the slave
    # failed to answer, the master spun forever with FM=1 and took the
    # 68K down with it — a dead machine. They are bounded now, so a
    # NONZERO count here is the hang, caught and survived: it says the
    # slave missed the preempt mailbox and the frame dropped instead.
    # Zero on a healthy run, so this is the hang localiser.
    t_pick = rd32(0x28000 + 21 * 4)
    t_echo = rd32(0x28000 + 22 * 4)
    if t_pick or t_echo:
        print(f"!! preempt-blit TIMEOUTS: pickup(SYNC2)={t_pick} "
              f"echo(SYNC5)={t_echo} — the slave missed the preempt "
              f"mailbox. Pre-1152c7d1 this was an unrecoverable HANG.")
    else:
        print(f"preempt-blit timeouts=0 (pickup/echo both answered)")
    # HV at the last blit-phase vint entry (md_main 0xFFB0FE, written
    # BEFORE the gate check). The gate accepts V in 0xDF..0xE2 (MAME-
    # tuned). If V clusters just past 0xE2 with the handler otherwise
    # fast -> the gate is mis-calibrated for ares (fires at ares's
    # natural H-int V), NOT a latency overrun. If V is deep in-frame
    # (0x00..0x20 / 0xF0+) -> genuine handler overrun. THE decider.
    hv = rdmd16(0xB0FE)
    v = hv >> 8
    gated = "REJECT" if (v < 0xDF or v > 0xE2) else "accept"
    print(f"HV at last vint={hv:04X} (V={v:02X} -> {gated}; "
          f"gate accepts DF..E2)")
    # ITER5 tail probe (0xFFB0F4): high byte = max whole-tail span, low
    # byte = max stream-section span, both in scanlines (post-window ->
    # end of the per-vint tail: DREQ push + palette scan + COMM stream).
    # Frame = 262. A whole-tail span at/over 262 on ares => the handler
    # overruns the frame -> the V-gate reject band. MAME floor ~227/120.
    # MAX total handler span (high byte) + the window/ack span of that
    # same worst vint (low byte), scanlines. tail = total - window.
    packed = rdmd16(0xB0F4)
    total, win = packed >> 8, packed & 0xFF
    real_total = total if total >= win else total + 256  # wrap past 256
    tail = (real_total - win) & 0x1FF
    dom = "WINDOW/ack-spin" if win > tail else "TAIL (DREQ+scan+stream)"
    verdict = ("LAPS THE FRAME -> entry drifts -> reject band"
               if real_total >= 262 else f"{262 - real_total} lines margin")
    print(f"worst handler: total={real_total} window/ack={win} tail={tail} "
          f"(frame=262) dominated by {dom} -> {verdict}")
    # LOOP 13 tearing lane (WINSPLIT builds): blit-only cost from
    # DIAG[23]/[25] (master ticks / master rows). Master blits 112 of
    # 224 rows per cycle (slave takes the rest concurrently), so
    # full-frame concurrent blit = 112 * ticks-per-row. MAME measured
    # 6.9 ticks/row = ~17 lines; vblank is 38 lines (1748 ticks). If
    # the ares figure fits with margin, the one-vblank full-frame blit
    # kills the band tear WITHOUT double-buffering.
    b23, b25 = rd32(0x28000 + 23 * 4), rd32(0x28000 + 25 * 4)
    if b25:
        tpr = b23 / b25
        full = 112 * tpr
        print(f"blit cost: {tpr:.1f} ticks/row -> full-frame "
              f"{full / 46.0:.1f} lines (vblank=38) -> "
              f"{'FITS: one-vblank blit is GO' if full < 1600 else 'no fit: write-log-ring road'}")
    # LOOP 13 MDVERIFY probe (make MDVERIFY=1): all state in WRAM
    # 0xFFA000 (v1 tallied at 0xFFB0EA, which the palette-scan span max
    # also writes — unreliable; do not trust v1 numbers).
    # [0] valid [1] addr [2] word [3] wrote [4] read [5] HV
    # [6] mismatches [7] stale packets [8] seq jumps [9] packets
    # LOOP 13 plane-A hunt (builds >= DRQR+1): cell records played per
    # plane by md_stage_play, and the port readback of the first NT-A
    # cell written each playback. On the bs9 that opened this hunt,
    # NT A was virgin-zero while its content sat in NT B; receive and
    # staging were exonerated from the dead stage buffer. If naplay is
    # high with rb_mm ~= naplay -> the DMA'd write to NT A is lost or
    # redirected at the VDP on ares. If rb_mm == 0 -> VRAM takes the
    # write and something wipes it later.
    naplay, nbplay = rdmd16(0xA024), rdmd16(0xA026)
    if naplay or nbplay:
        print(f"cell records played: NT A={naplay} NT B={nbplay} "
              f"NT-A readback last=0x{rdmd16(0xA028):04X} "
              f"mismatches={rdmd16(0xA02A)}")
    # wipe recheck: the SAME cell re-read at the NEXT vint's top, a
    # frame of game execution later. immediate-readback ok + recheck
    # mismatch = plane A is zeroed MID-FRAME by a non-shim writer.
    rcn = rdmd16(0xA034)
    if rcn:
        rcm = rdmd16(0xA030)
        print(f"NT-A wipe recheck: {rcm}/{rcn} mismatches "
              f"({100.0 * rcm / rcn:.1f}%) last=0x{rdmd16(0xA032):04X}"
              + (" -> MID-FRAME WIPE CONFIRMED" if rcm else
                 " -> survives the frame"))
    pkts = rdmd16(0xA012)
    if pkts:
        mm, stale, jumps = rdmd16(0xA00C), rdmd16(0xA00E), rdmd16(0xA010)
        print(f"MDVERIFY: packets={pkts} write-mismatches={mm} "
              f"STALE re-reads={stale} ({100.0 * stale / pkts:.1f}%) "
              f"seq jumps={jumps}")
        if rdmd16(0xA000):
            print(f"  first bad write: vram=0x{rdmd16(0xA002):04X} "
                  f"word={rdmd16(0xA004)} wrote=0x{rdmd16(0xA006):04X} "
                  f"read=0x{rdmd16(0xA008):04X} HV=0x{rdmd16(0xA00A):04X}")
    else:
        print("MDVERIFY: no data (only meaningful on a MDVERIFY=1 build)")


if __name__ == "__main__":
    main()
