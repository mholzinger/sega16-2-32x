// jt51_ref -- render a YM2151 register stream through jt51 (Jotego's RTL, the YM2151 the
// jtcores System 16B core runs) under Verilator: the oracle for sndtest/opm. KIT.
//
//   jt51_ref EVENTS.bin OUT.s16 NSAMPLES
//
// EVENTS.bin: records of <u32 sample, u8 reg, u8 val>, little-endian, sample = index at the
// chip's own rate (clock/64; the harness runs one clk per YM2151 clock, so the clock
// only matters to whoever made the event times). OUT.s16: NSAMPLES of xleft, int16 LE, one per `sample`
// strobe -- the output jts16b_snd.v:212 takes. Built by tools/opm_ab.py, which verilates
// the RTL straight out of srcref/jtcores/modules/jt51 (git show; nothing is copied here).
//
// Clocking as jts16b_snd.v:197-198 wires it: cen every clk, cen_p1 every other one. Writes
// go through the CPU port (a0=0 address, a0=1 data) and wait out the busy flag, as a
// driver would.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "Vjt51.h"
#include "verilated.h"

static Vjt51 *top;
static uint64_t ticks;
static int16_t *out;
static uint32_t nout, want;
static uint8_t last_sample;
static int kon_align = -1;                     // JT51_KON_ALIGN: ticks after the sample strobe

static void tick() {
	top->cen = 1;
	top->cen_p1 = (ticks & 1) ? 1 : 0;
	top->clk = 0; top->eval();
	top->clk = 1; top->eval();
	ticks++;
	if (top->sample && !last_sample && nout < want)
		out[nout++] = (int16_t)top->xleft;
	last_sample = top->sample;
}

static void port_write(int a0, uint8_t v) {
	top->a0 = a0; top->din = v; top->cs_n = 0; top->wr_n = 0;
	tick(); tick();
	top->cs_n = 1; top->wr_n = 1;
	tick(); tick();
}

static void wait_busy() {
	top->a0 = 1; top->cs_n = 0; top->wr_n = 1;
	for (int g = 0; g < 4096; g++) {
		tick();
		if (!(top->dout & 0x80)) break;
	}
	top->cs_n = 1;
}

int main(int argc, char **argv) {
	if (argc != 4) { fprintf(stderr, "usage: jt51_ref EVENTS.bin OUT.s16 NSAMPLES\n"); return 2; }
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	std::vector<uint8_t> ev;
	uint8_t buf[6];
	while (fread(buf, 1, 6, f) == 6) ev.insert(ev.end(), buf, buf + 6);
	fclose(f);
	want = (uint32_t)strtoul(argv[3], 0, 0);
	out = (int16_t *)calloc(want, 2);

	if (getenv("JT51_KON_ALIGN")) kon_align = atoi(getenv("JT51_KON_ALIGN"));
	Verilated::commandArgs(argc, argv);
	top = new Vjt51;
	top->cs_n = 1; top->wr_n = 1; top->a0 = 0; top->din = 0;
	top->rst = 1;
	for (int i = 0; i < 256; i++) tick();
	top->rst = 0;
	nout = 0;                                   // sample 0 = the first strobe after reset, so
	                                            // jt51's envelope counter starts where opm.c's does

	size_t e = 0;
	uint64_t write_clocks = 0, writes = 0;
	while (nout < want) {
		while (e < ev.size()) {
			uint32_t t = ev[e] | ev[e + 1] << 8 | ev[e + 2] << 16 | (uint32_t)ev[e + 3] << 24;
			if (t > nout) break;
			uint64_t t0 = ticks;
			wait_busy();
			if (kon_align >= 0) {
				// pace EVERY write to a fixed point of the pass (JT51_KON_ALIGN ticks after
				// the sample strobe): one write per sample, as sndtest/opm/opm_render spaces
				// them, so a burst lands on the same samples here and there. jt51_kon.v hands
				// a key-on to each operator slot as the slot passes, so the point also decides
				// whether the four operators key in one pass; 56 puts the write after C2's slot
				for (;;) {                   // tick until the strobe's rising edge
					uint8_t prev = last_sample;
					tick();
					if (top->sample && !prev) break;
				}
				for (int i = 0; i < kon_align; i++) tick();
			}
			port_write(0, ev[e + 4]);
			port_write(1, ev[e + 5]);
			write_clocks += ticks - t0;
			writes++;
			e += 6;
		}
		tick();
	}
	// the cost per write (busy wait + two port cycles) is what sndtest/opm/opm_render's
	// --write-clocks must match for a burst to land on the same samples here and there
	fprintf(stderr, "jt51_ref: %llu clocks for %u samples (%.3f per sample); %llu writes, %.1f clocks each\n",
	        (unsigned long long)ticks, nout, (double)ticks / nout,
	        (unsigned long long)writes, writes ? (double)write_clocks / writes : 0.0);
	FILE *o = fopen(argv[2], "wb");
	fwrite(out, 2, want, o);
	fclose(o);
	delete top;
	return 0;
}
