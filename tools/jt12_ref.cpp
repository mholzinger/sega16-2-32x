// jt12_ref -- render a YM2612 port-write stream through jt12 (Jotego's RTL, the YM2612 the
// MiSTer Mega Drive core runs) under Verilator: the hardware-truth proxy for the cart's FM
// lane. KIT.
//
//   jt12_ref EVENTS.bin OUT.s16 NSAMPLES
//
// EVENTS.bin: records of <u32 sample, u8 port, u8 val>, little-endian; port 0/2 = address
// (bank 0/1), 1/3 = data; sample = index at the chip's own rate (clock/144, 53267 Hz at the
// Mega Drive's 7.67 MHz). OUT.s16: NSAMPLES of snd_left, int16 LE, one per snd_sample.
// Built by tools/opm_ab.py-style callers; the RTL comes from srcref/jtcores/modules/jt12
// via git show, nothing is copied here. Writes wait out the busy flag as a driver would.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "Vjt12.h"
#include "verilated.h"

static Vjt12 *top;
static uint64_t ticks;
static int16_t *out;
static uint32_t nout, want;
static uint8_t last_sample;

static void tick() {
	top->cen = 1;
	top->clk = 0; top->eval();
	top->clk = 1; top->eval();
	ticks++;
	if (top->snd_sample && !last_sample && nout < want)
		out[nout++] = (int16_t)top->snd_left;
	last_sample = top->snd_sample;
}

static void port_write(int addr, uint8_t v) {
	top->addr = addr; top->din = v; top->cs_n = 0; top->wr_n = 0;
	tick(); tick();
	top->cs_n = 1; top->wr_n = 1;
	tick(); tick();
}

static void wait_busy() {
	top->addr = 0; top->cs_n = 0; top->wr_n = 1;
	for (int g = 0; g < 4096; g++) {
		tick();
		if (!(top->dout & 0x80)) break;
	}
	top->cs_n = 1;
}

int main(int argc, char **argv) {
	if (argc != 4) { fprintf(stderr, "usage: jt12_ref EVENTS.bin OUT.s16 NSAMPLES\n"); return 2; }
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	std::vector<uint8_t> ev;
	uint8_t buf[6];
	while (fread(buf, 1, 6, f) == 6) ev.insert(ev.end(), buf, buf + 6);
	fclose(f);
	want = (uint32_t)strtoul(argv[3], 0, 0);
	out = (int16_t *)calloc(want, 2);

	Verilated::commandArgs(argc, argv);
	top = new Vjt12;
	top->cs_n = 1; top->wr_n = 1; top->addr = 0; top->din = 0; top->en_hifi_pcm = 0;
	top->rst = 1;
	for (int i = 0; i < 2048; i++) tick();
	top->rst = 0;
	nout = 0;
	size_t e = 0;
	uint64_t writes = 0, write_clocks = 0;
	while (nout < want) {
		while (e < ev.size()) {
			uint32_t t = ev[e] | ev[e + 1] << 8 | ev[e + 2] << 16 | (uint32_t)ev[e + 3] << 24;
			if (t > nout) break;
			uint64_t t0 = ticks;
			wait_busy();
			port_write(ev[e + 4], ev[e + 5]);
			write_clocks += ticks - t0;
			writes++;
			e += 6;
		}
		tick();
	}
	fprintf(stderr, "jt12_ref: %llu clocks for %u samples (%.3f per sample); %llu port writes, %.1f clocks each\n",
	        (unsigned long long)ticks, nout, (double)ticks / nout, (unsigned long long)writes,
	        writes ? (double)write_clocks / writes : 0.0);
	FILE *o = fopen(argv[2], "wb");
	fwrite(out, 2, want, o);
	fclose(o);
	delete top;
	return 0;
}
