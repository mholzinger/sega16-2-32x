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
    # DISPLAY GATE census (2026-09-05): blanks entered / vints held blank
    # after the game re-enabled its display (= our load lag, the number
    # to shrink). Zero on builds before the gate.
    # lost-push belt health: tears vs echoes vs 68K re-marks
    print(f"BELT: torn landings={rd32(0x28F80 + 7*4)} echoes raised={rd32(0x28F80 + 11*4)} "
          f"68K re-marks={rdmd16(0xA0B2)}  (echoes-torn = sequence-gap echoes; re-marks should equal echoes)")
    dc = rd32(0x28F7C)
    if dc:
        print(f"DISPLAY GATE: blanks={dc >> 16} held-after-display-on={dc & 0xFFFF} vints"
              f" (avg {(dc & 0xFFFF) / max(1, dc >> 16):.1f}/cut; arcade shows the scene 1 vint after)")
    # LOST-PUSH DETECTOR (2026-09-05, the JP black boss): a palette word
    # the 68K shadow (0xFF6000) already holds equal to the game's mirror
    # (0xFF9000) while the SH-2's PAL_SH (SDRAM 0x27000) still differs
    # was shipped once, lost on the FIFO, and will never be re-sent by
    # the delta path. Glow words 0x98-0xAF are masked by design.
    try:
        lost = [w for w in range(2048)
                if not (0x98 <= w <= 0xAF)
                and rdmd16(0x6000 + w * 2) == rdmd16(0x9000 + w * 2)
                != struct.unpack(">H", swap16(st[sd + 0x27000 + w * 2:sd + 0x27000 + w * 2 + 2]))[0]]
        pend = sum(1 for w in range(2048) if not (0x98 <= w <= 0xAF)
                   and rdmd16(0x6000 + w * 2) != rdmd16(0x9000 + w * 2))
        print(f"LOST-PUSH palette words (shadow==game, PAL_SH stale)={len(lost)}"
              f"{' at ' + ' '.join(f'{w:03x}' for w in lost[:16]) if lost else ''}"
              f"  pending(unshipped)={pend}  << any nonzero = a sprite/tile set wearing stale colour")
    except Exception as e:
        print(f"LOST-PUSH: n/a ({e})")
    cycles = rd32(0x28000 + 9 * 4)
    skips = rd32(0x28000 + 7 * 4)
    gates = rdmd16(0xB0FC)
    print(f"BUILD: {rd32(0x28000 + 18 * 4):08x}")
    # PHASECENSUS=1 builds: per-generation wall split (0x28E40, master FRT)
    ph = [rd32(0x28E40 + i * 4) for i in range(16)]
    if ph[10] or ph[0]:
        g = max(1, rd32(0x28F54)); f = max(1, ph[10]); V = 12052.0
        print(f"phase(v/gen): echo {ph[0]/g/V:.2f} (max {ph[5]/V:.2f}) "
              f"mtask {ph[1]/g/V:.2f} (max {ph[6]/V:.2f}) "
              f"lag {ph[2]/g/V:.2f} (max {ph[7]/V:.2f}) "
              f"ship {ph[3]/g/V:.2f} (max {ph[8]/V:.2f}) "
              f"flip {ph[4]/f/V:.2f} (max {ph[9]/V:.2f}) "
              f"| gens {rd32(0x28F54)} flips {ph[10]} "
              f"wall {rd32(0x28F50)/g/V:.2f}")
        acks = max(1, vints)
        print(f"  window: landing {rd32(0x28FF8)/max(1,rd32(0x28FFC))/V:.2f}v "
              f"blit-done {rd32(0x28E6C)/g/V:.2f}v ack {rd32(0x28E3C)/acks/V:.2f}v "
              f"launch {rd32(0x28E38)/g/V:.2f}v (offsets into the vint) "
              f"| launch->slave pickup {rd32(0x28E7C)/g/V:.2f}v")
        hb = [struct.unpack(">H", swap16(st[sd + 0x39900 + i * 2:sd + 0x39900 + i * 2 + 2]))[0] for i in range(16)]
        print(f"  echo bins 0.5..1.5v/0.125: {' '.join(str(x) for x in hb[:8])} "
              f"| mtask bins: {' '.join(str(x) for x in hb[8:])}")
    print(f"vints={vints} cycles={cycles} -> "
          f"vints/cycle={vints / max(cycles, 1):.2f} "
          f"({'20Hz - DEAD, canonical is 30' if vints / max(cycles, 1) > 2.5 else '30Hz - the floor; 60 is the target' if vints / max(cycles, 1) > 1.5 else '60Hz - THE TARGET'})")
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
    # SLAVE IDLE METER (LOOP16 motion-parity lane, builds 85802cde+):
    # no-command poll visits on the slave's loop. Relative meter —
    # divide by cycles to compare builds/scenes; high = the second
    # SH-2 has unused capacity for more compose rows / cadence.
    sidle = rd32(0x28FA8)
    if sidle:
        print(f"slave idle polls={sidle} ({sidle / max(cycles, 1):.0f}/cycle) "
              f"<< motion-parity capacity meter")
    # WINSPAN (LOOP15): MD packet-consume span accumulator, builds
    # de463cb1+. Relative meter (V-jump samples discarded) — rank
    # builds with it, don't read it as an absolute clock.
    ws_n = rdmd16(0xA03C)
    if ws_n:
        ws_sum = (rdmd16(0xA038) << 16) | rdmd16(0xA03A)
        print(f"MD consume span: mean={ws_sum / ws_n:.1f} lines "
              f"max={rdmd16(0xA03E)} (n={ws_n}) << the 68K window lever")
    # LOOP 17 — MD_PAYOFF: what fraction of the framebuffer is EMPTY.
    # Only meaningful on a MDPAYOFF=1 build. The question it answers is
    # whether "ship 320x224 every frame" is still a real constraint now
    # that MDBGALL moved the background onto the MD plane: if most rows
    # come out entirely transparent, the blit can stop shipping them and
    # the frame stops having to fit inside a vint.
    # LOOP 9 measured 13-17% skippable — BEFORE the pivot, with the BG
    # still in the framebuffer dirtying every row. That number is stale.
    pay_l, pay_l0 = rd32(0x28000 + 60 * 4), rd32(0x28000 + 61 * 4)
    pay_g, pay_g0 = rd32(0x28000 + 62 * 4), rd32(0x28000 + 63 * 4)
    pay_r, pay_r0 = rd32(0x28FBC), rd32(0x28FC0)
    # GHOST GUARD (2026-08-30, Mike's live PALDELTA state): on builds
    # without MDPAYOFF these DIAG slots hold OTHER tenants and the block
    # printed 3648%/428814% "transparency". A ratio over 100% is
    # self-evidently not a fraction of anything — suppress the block
    # instead of dressing garbage as headroom.
    if pay_l and (pay_l0 > pay_l or pay_g0 > pay_g or pay_r0 > pay_r):
        print("MD_PAYOFF: no data (slots 60-63 reused by another tenant — "
              "not an MDPAYOFF build; ghost reading suppressed)")
        pay_l = 0
    if pay_l:
        print(f"MD_PAYOFF transparency (blit-skip headroom):")
        print(f"  area      {100.0 * pay_l0 / max(pay_l, 1):5.1f}% of longs "
              f"(4px each) are zero")
        print(f"  32px grps {100.0 * pay_g0 / max(pay_g, 1):5.1f}% fully "
              f"transparent  << what a smarter blit could skip in-row")
        if pay_r:
            print(f"  WHOLE ROWS{100.0 * pay_r0 / max(pay_r, 1):5.1f}% "
                  f"entirely transparent ({pay_r0}/{pay_r}) "
                  f"<< the blit could stop shipping these ENTIRELY")
        print(f"  (probe reads every row twice — IGNORE all timings on "
              f"this build; LOOP 9's pre-MDBGALL figure was 13-17%)")
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
        if dlast == 596 and dmax == 596:
            print("  -> residue==596 across cycles on an FMGATE build is "
                  "EXPECTED: the k1 push is\n     skipped BY DESIGN "
                  "(LOOP23) and every k2 pickup reads landed=0. Not a "
                  "fault.")
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
    # PASS 11d — the purple-band tripwire: verify-per-row audits one
    # skipped group per skipping row; a nonzero count = the BLITSKIP
    # bank mask LIED (the live-only desync) and was healed in-place.
    bml = rd32(0x28000 + 42 * 4)
    print(f"blitskip mask lies (verify-per-row, healed)={bml}"
          f"{'  << THE PURPLE-BAND DESYNC, CAUGHT LIVE' if bml else ''}")
    # PASS 12 — deferral attribution: which band pays. R2 = rows 144-224
    # (the lower-third tear), R0/R1 = upper/mid. Chain order R0->R1->R2
    # predicts R2 dominant; a different shape redirects the fix.
    d0, d1, d2 = rd32(0x28FC8), rd32(0x28FCC), rd32(0x28FD0)
    ow0, ow1 = rd32(0x28FF0), rd32(0x28FF4)
    if ow0 or ow1:
        # SLOT MEANING CHANGED 2026-08-30 (arc B): FF0/F4 = CRAM
        # REMAP vs VALUE paints (cram_memo misses). Earlier same-day
        # builds briefly used them for the retired owed census.
        print(f"CRAM paints: REMAP={ow0} (violent recolor class — the "
              f"blink strips are COLOR events) vs VALUE={ow1} (fades)")
    print(f"deferrals by band: R0(top)={d0} R1(mid)={d1} R2(low)={d2}"
          f"{'  << R2 = the lower-third tear' if d2 > d0 + d1 else ''}")
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
    # RETIRED 2026-08-29: this block read DIAG[26]/[27] as the pres-1.x
    # restore-past-vblank pair, but those slots were REUSED long ago
    # ([26] = sprite-half tracer, [27] = slave echo timeouts) — every
    # "STROBE CONFIRMED" banner since was a GHOST misread (it triggered
    # a full false investigation of a top-of-screen flip split that
    # frame-exact headless capture proved does not exist). The restore
    # class is extinct by construction under pres 2.0; nothing to read.
    # RETIRED 2026-08-29 (same sweep as the restore block above): the
    # "FS restore latch" lines read DIAG[29]/[30], which were REUSED
    # long ago ([29] = ROW_DEFER deferred rows, [30] = compose-skip
    # count) — its "waits >1 line" percentages in recent dumps were
    # compose-skip counts wearing a latch costume. Ghost misread #5;
    # [31] (k2 late latches) remains real and prints with VISRFLIP.
    # LOOP 24 — VISRFLIP ISR-cost probe family (zero on non-probe builds).
    # The master's V-ISR waits <=700 ticks for the 68K's k2 post and runs
    # the flip span from vblank entry; a bail leaves the cycle on the
    # LOOP23 polled path. Read WITH [31] and [7]: the probe's whole claim
    # is flip-late-latches -> ~0 while skips/cadence hold.
    #   [49] fires  [58] ISR flips  [56] body-fallback flips
    #   [59] stale-COMM0 bails  [60] no-post bails  [61] k1 posts seen
    #   [62] max ISR ticks (wait+span)  [63] sum over ISR flips
    v_fire = rd32(0x28000 + 49 * 4)
    v_flip = rd32(0x28000 + 58 * 4)
    if v_fire and v_flip:
        v_fb, v_st, v_np, v_k1 = (rd32(0x28000 + i * 4)
                                  for i in (56, 59, 60, 61))
        v_max, v_sum = rd32(0x28000 + 62 * 4), rd32(0x28000 + 63 * 4)
        print(f"VISRFLIP: fires={v_fire} isr-flips={v_flip} "
              f"body-fallback={v_fb} "
              f"({100.0 * v_flip / max(v_flip + v_fb, 1):.1f}% of flips "
              f"in-ISR) bails: stale={v_st} no-post={v_np} k1-seen={v_k1} "
              f"isr span: mean={v_sum / v_flip / 46.0:.1f} lines "
              f"max={v_max / 46.0:.0f} lines (vblank=38; span includes "
              f"the <=15-line post wait; the FLIP sits mid-span, before "
              f"the restore half)")
        # K2FREE additions (zero on plain VISRFLIP builds): [45]/[46]
        # flip POSITION from ISR entry (the tear-legality number, vs
        # the span which includes the restore), [47] k2 seen too late
        # to flip. 68K side: push aborts 0xB0E0 (echo-gate + FIFO
        # exhaustion — reject-correlated is healthy), consume-B
        # deferrals 0xB0EE (one-cycle-late MD packets, healed).
        fp_sum, fp_max = rd32(0x28000 + 45 * 4), rd32(0x28000 + 46 * 4)
        if fp_sum:
            print(f"K2FREE: flip-pos mean={fp_sum / v_flip / 46.0:.1f} "
                  f"lines max={fp_max / 46.0:.0f} (vblank=38; mean is "
                  f"tail-skewed by deep-watch drains — read WITH [31]) "
                  f"late-k2[47]={rd32(0x28000 + 47 * 4)} "
                  f"push-aborts={rdmd16(0xB0E0)} "
                  f"consumeB-deferrals={rdmd16(0xB0EE)}")
    # LOOP 25 — PALETTE STORM PROBE (PALSTORM=1 builds only; zero
    # otherwise). The channel ships <= 4 blocks per k2 push; a storm
    # that dirties faster leaves CRAM a torn generation-mix (the black
    # smoke / inverted-flash family). backlog>KMAX vints are the torn
    # exposure; max-backlog / 4 ~= cycles of tear per storm.
    ps_n = rdmd16(0xA074)
    if ps_n:
        print(f"PALSTORM: vints={ps_n} backlog now={rdmd16(0xA060)} "
              f"max={rdmd16(0xA062)} "
              f"(tile-half max={rdmd16(0xA070)} sprite-half max={rdmd16(0xA072)}) "
              f"dirtied sum={(rdmd16(0xA064) << 16) | rdmd16(0xA066)} "
              f"shipped sum={(rdmd16(0xA068) << 16) | rdmd16(0xA06A)} "
              f"storm vints={rdmd16(0xA06C)} "
              f"torn-exposure vints={rdmd16(0xA06E)} "
              f"({100.0 * rdmd16(0xA06E) / ps_n:.1f}%)")
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
    # LOOP15: always-on handler-total mean (builds >= the wrapB4
    # successor). THE speed number: mean 68K lines/vint taken from the
    # game; game CPU vs arcade ~= 0.77 x (262 - mean) / 262.
    hsum = (rdmd16(0xB0D0) << 16) | rdmd16(0xB0D2)
    if hsum and vints:
        hmean = hsum / vints
        print(f"68K handler mean={hmean:.1f} lines/vint "
              f"-> game gets ~{100.0 * (262 - hmean) / 262:.0f}% of the MD 68K "
              f"(~{77.0 * (262 - hmean) / 262:.0f}% of the 10MHz arcade)")
        wsum = (rdmd16(0xA040) << 16) | rdmd16(0xA042)
        if wsum:
            wmean = wsum / vints
            # R60 CAVEAT (2026-08-30): v_win is stamped AFTER r60_push,
            # so under R60 this "window/ack" span swallows the 68K's OWN
            # selection+ship — it is NOT the master's FM work there. The
            # r60 section stamps below carry the true split.
            note = (" [R60: span INCLUDES the 68K's own selection+push — "
                    "read the r60 sections below, not this label]"
                    if wmean > 0.9 * hmean else "")
            print(f"  split: window/ack-wait mean={wmean:.1f} "
                  f"(pre-R60: master's FM work) vs own-tail "
                  f"mean={hmean - wmean:.1f} (consume+DREQ push+glue) "
                  f"<< the bigger half names the next surgery{note}")
        # R60 PUSH SECTION STAMPS (PALDELTA era): last-value HV at
        # r60_push boundaries, 0xFFA0B4..BE (+ ncmp/ndirty 0xA0D2/D4).
        # One vint's autopsy, not a mean — but it is the instrument
        # that convicted the compare pre-pass (55 lines of uint16-
        # indexed C) and the one that watches the asm-shaping slice.
        def vlin(hv):
            v = hv >> 8
            return v - 0xE5 + 235 if v >= 0xE5 else v
        sect = [("rotor+cmp", 0xA0BE), ("nrec+rs", 0xA0B6),
                ("regs", 0xA0B8), ("pal", 0xA0BA), ("recs+tail", 0xA0BC)]
        prev = rdmd16(0xA0B4)
        if prev:
            parts = []
            for name, addr in sect:
                cur = rdmd16(addr)
                parts.append(f"{name}={(vlin(cur) - vlin(prev)) % 262}")
                prev = cur
            print(f"  r60 sections (last vint, lines): {' '.join(parts)}  "
                  f"ncmp={rdmd16(0xA0D2)} ndirty={rdmd16(0xA0D4)}")
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
        # LOOP 18: THE TEARING DECISION. The seam is structural — the k2
        # flip runs BEFORE k2's blit, so a displayed bank holds its top
        # half and bottom half from snapshots ~50ms apart. Moving the
        # flip AFTER that blit makes both halves one snapshot and kills
        # the seam, but the FBCTL write must still land inside vblank or
        # ares defers it a whole frame (LOOP 7c). Only ONE window's blit
        # has to precede it — 56 master rows under WIN_TWO, not 112.
        win = 56 * tpr / 46.0
        room = 38.0 - win
        print(f"flip-after-blit: k2 blit = {win:.1f} lines of the 38-line "
              f"vblank, {room:.1f} lines of room -> "
              + ("AFFORDABLE: move the flip, the seam dies"
                 if room >= 8 else
                 ("MARGINAL: needs >=8 lines of room to be safe"
                  if room > 0 else "NO: the blit does not fit at all")))
    # LOOP 18: the question BLITSKIP raised. DIAG[24] is the post-blit
    # WAIT (SYNC[2] pickup + the flip latch spin + the SYNC[5] echo) --
    # slot 5 has always bundled it WITH the blit, so "window/ack-wait"
    # above cannot say whether the master is BLITTING or WAITING FOR THE
    # SLAVE. BLITSKIP cut 57% of the blit's group stores and moved the
    # handler mean only 91.3 -> 87.8 while slave idle ROSE 13.8k ->
    # 16.4k/cycle, which is what it looks like when the master's window
    # is bounded by a data dependency on the slave's compose rather than
    # by its own byte count (LOOP 9 PICKUP_SRC_PROBE found exactly that
    # at k=1). If wait dominates here, the next lever is the SLAVE, not
    # fewer bytes -- and shrinking the blit further buys nothing.
    b24 = rd32(0x28000 + 24 * 4)
    if b23 or b24:
        tot = b23 + b24
        print(f"window split: blit={b23 / 46.0 / max(vints, 1):.1f} "
              f"lines/vint vs post-blit wait={b24 / 46.0 / max(vints, 1):.1f} "
              f"lines/vint  ({100.0 * b23 / tot:.0f}% blit / "
              f"{100.0 * b24 / tot:.0f}% wait)"
              f" << if WAIT dominates, the blit is no longer the constraint")
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
