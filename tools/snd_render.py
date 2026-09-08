#!/usr/bin/env python3
"""S16B sound-board renderer — runs the real Z80 driver, captures YM2151.
KIT (2026-09-01). See SOUND_DRIVER.md.

Boots the sound driver ROM in tools/z80cpu.py inside a faithful S16B
sound-board harness (memory map + ports from MAME segas16b.cpp:1813-1837,
uPD bank from :1151-1217), posts one sound command, runs for N ticks of
the driver's own Timer-A clock, and emits the YM2151 register-write log
in the exact tap format tools/soundmap_build.py / opm2opn.py consume
(`<t> Y0 <reg>` / `<t> Y1 <val>`, plus PC/PD for uPD7759). Faithful by
construction — the driver's LFO/portamento/envelope re-application all
execute for real, which the isolation tap dropped (HANDOFF-SOUND.md).

  snd_render.py PROG_ROM --cmd 0x94 --ticks 15000 [--samples s1 s2] \
      [--ym-clock 4000000] [--out log]
"""
import argparse
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from z80cpu import Z80                                       # noqa: E402


class Board:
    def __init__(self, prog, samples, ym_clock):
        # soundcpu region: program in [0,0x8000), samples appended at
        # 0x10000 (segas16b.cpp:1215 set_base(base+0x10000+bankoffs)).
        self.region = bytearray(0x10000)
        self.region[0:len(prog)] = prog
        smp = b"".join(samples)
        self.region += smp
        self.smp_size = len(smp) or 1
        self.ram = bytearray(0x800)
        self.cmd = 0x80              # idle latch = 0x80: driver no-op (NOT 0x01,
        self.bank = 0                # banked-ROM base into sample area
        self.ym_addr = 0
        self.ym_status = 0           # bit0=TimerA flag, bit7=busy(=0)
        self.ta = 0                  # Timer A 10-bit value
        self.ym_clock = ym_clock
        self.ta_deadline = None      # cpu-cycle deadline for next TA flag
        self.ta_irq_en = False       # reg $14 bit2 (Timer A IRQ enable)
        self.pending_irq = False     # TA overflowed, IRQ owed to the CPU
        self.events = []             # (cycle, kind, val)
        self.tick = 0

    # ---- memory bus ----------------------------------------------
    def read8(self, a):
        a &= 0xFFFF
        if a < 0x8000:
            return self.region[a]
        if a < 0xE000:                          # banked sample ROM
            off = 0x10000 + ((self.bank + (a - 0x8000)) % self.smp_size)
            return self.region[off] if off < len(self.region) else 0xFF
        if a == 0xE800:                         # sound-command latch (mem)
            v = self.cmd
            self.cmd = 0x80                      # idle = 0x80 no-op (0x01-0x40 = VOLUME cmds!)
            return v
        if a >= 0xF800:
            return self.ram[a - 0xF800]
        return 0xFF

    def write8(self, a, v):
        a &= 0xFFFF
        if a >= 0xF800:
            self.ram[a - 0xF800] = v & 0xFF
        # ROM / bank / latch writes: ignored

    # ---- I/O ports (global mask 0xFF; decode by a&0xC0) ----------
    def port_in(self, p):
        p &= 0xFF
        sel = p & 0xC0
        if sel == 0x00:                         # YM2151 status
            return self.ym_status
        if sel == 0x80:                         # uPD7759 status (bit7 busy)
            return 0x00
        if sel == 0xC0:                         # command latch (read-once)
            v = self.cmd
            self.cmd = 0x80
            return v
        return 0xFF

    def port_out(self, p, v):
        p &= 0xFF
        v &= 0xFF
        sel = p & 0xC0
        if sel == 0x00:                         # YM2151
            if (p & 1) == 0:
                self.ym_addr = v                # address latch
            else:
                self._ym_write(self.ym_addr, v)
        elif sel == 0x40:                       # uPD control + bank
            self._upd_control(v)
            self.events.append((self.cyc, "PC", v))
        elif sel == 0x80:                       # uPD data
            self.events.append((self.cyc, "PD", v))
        # 0xC0: latch write (unused by driver)

    def _ym_write(self, reg, val):
        self.events.append((self.cyc, "Y0", reg))
        self.events.append((self.cyc, "Y1", val))
        if reg == 0x10:                         # Timer A high 8 bits
            self.ta = (self.ta & 0x03) | (val << 2)
        elif reg == 0x11:                       # Timer A low 2 bits
            self.ta = (self.ta & ~0x03) | (val & 0x03)
        elif reg == 0x14:                       # timer control/reset
            # bit0 = load TA, bit4 = reset TA flag, bit2 = TA IRQ enable
            self.ta_irq_en = bool(val & 0x04)
            if val & 0x10:
                self.ym_status &= ~0x01
            if val & 0x01:
                period = 64 * (1024 - self.ta)  # YM clocks
                # scale to Z80 cycles (both boards clock ~ equal here)
                self.ta_deadline = self.cyc + period
                self.tick += 1

    def _upd_control(self, data):
        # segas16b.cpp:1151-1217 default variant (Altered Beast)
        off = 0
        if not (data & 0x04): off = 0x00000
        if not (data & 0x08): off = 0x10000
        if not (data & 0x10): off = 0x20000
        if not (data & 0x20): off = 0x30000
        off += (data & 0x03) * 0x4000
        self.bank = off % self.smp_size


def render(prog, samples, cmd, max_ticks, ym_clock, boot_ticks=64):
    board = Board(prog, samples, ym_clock)
    board.cyc = 0
    cpu = Z80(board.read8, board.write8, board.port_in, board.port_out)
    cpu.pc = 0
    cpu.sp = 0x0000

    def sync():
        board.cyc = cpu.cycles
        # Timer A overflow -> status bit0 (drives the tick wait at $0CE1)
        # AND the maskable IRQ ($0038 -> $0089 reads the command latch).
        if (board.ta_deadline is not None and cpu.cycles >= board.ta_deadline
                and not (board.ym_status & 0x01)):
            board.ym_status |= 0x01
            if board.ta_irq_en:
                board.pending_irq = True
        if board.pending_irq and cpu.iff1:
            board.pending_irq = False
            cpu.interrupt()

    # boot: run until the driver has enabled Timer A and settled
    guard = 0
    while board.tick < boot_ticks and guard < 40_000_000:
        cpu.step(); sync(); guard += 1
    # post the command once; the latch is read-once (cleared on read),
    # so the IRQ intake queues it a single time, like the real board.
    board.cmd = cmd
    start_tick = board.tick
    guard = 0
    while board.tick - start_tick < max_ticks and guard < 400_000_000:
        cpu.step(); sync()
        guard += 1
    return board


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("prog")
    ap.add_argument("--cmd", type=lambda x: int(x, 0), default=0x94)
    ap.add_argument("--ticks", type=int, default=15000)
    ap.add_argument("--samples", nargs="*", default=[])
    ap.add_argument("--ym-clock", type=int, default=4000000)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()
    prog = open(args.prog, "rb").read()
    samples = [open(s, "rb").read() for s in args.samples]
    board = render(prog, samples, args.cmd, args.ticks, args.ym_clock)
    # tick period in seconds for timestamping (Timer A)
    period_s = 64 * (1024 - board.ta) / args.ym_clock if board.ta else 1e-3
    # cycles -> seconds: cycle count advances with the tick deadline model
    out = open(args.out, "w") if args.out else sys.stdout
    ymn = 0
    for cyc, kind, val in board.events:
        t = (cyc / (64 * (1024 - board.ta))) * period_s if board.ta else cyc * 1e-7
        out.write(f"{t:.7f} {kind} {val:02X}\n")
        if kind == "Y1":
            ymn += 1
    if args.out:
        out.close()
    print(f"rendered cmd 0x{args.cmd:02X}: {ymn} YM writes, "
          f"{board.tick} ticks, TA={board.ta} "
          f"(tick={period_s*1000:.3f}ms)", file=sys.stderr)


if __name__ == "__main__":
    main()
