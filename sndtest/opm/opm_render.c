/* opm_render -- host driver for opm.c: render a register stream to int16 LE. KIT.
 *
 *   opm_render EVENTS.bin OUT.s16 NSAMPLES RATE CHMASK CLOCK [STEPS.bin] [LATENCY] [WRITESAMPLES]
 *
 * EVENTS.bin is tools/opm_ab.py's format: <u32 native sample, u8 reg, u8 val> records,
 * times at CLOCK/64. RATE 0 renders at that native rate; otherwise each write lands on
 * the first output sample at or after its time. STEPS.bin (768 u32 LE) replaces the
 * phase-step table: tools/opm_ab.py --rom-steps feeds jt51's own ROM to separate the
 * formula table's pitch from everything else. LATENCY (native samples, default 0) is how
 * long after its due time a write takes effect, and WRITESAMPLES (default 1) how many
 * samples one write occupies; a write lands at max(due + LATENCY, previous + WRITESAMPLES).
 * tools/jt51_ref.cpp under JT51_KON_ALIGN paces every write to the sample strobe, which is
 * 2 and 2 (measured: the envelope then steps on the same key-on phases as jt51's, for every
 * operator slot, in bursts and alone). The chip's busy flag makes WRITESAMPLES at least 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include "opm.h"

int main(int argc, char **argv)
{
	static opm_chip chip;
	FILE *f, *o;
	unsigned char b[6];
	unsigned char *ev = 0;
	size_t nev = 0, cap = 0, e = 0;
	uint32_t n, rate, mask, clock, i, latency = 0, wsamp = 1, per;
	uint64_t next_ok = 0;
	int16_t *out;
	static uint32_t steps[768];

	if (argc < 7 || argc > 10) {
		fprintf(stderr, "usage: opm_render EVENTS.bin OUT.s16 NSAMPLES RATE CHMASK CLOCK [STEPS.bin] [LATENCY] [WRITESAMPLES]\n");
		return 2;
	}
	f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	while (fread(b, 1, 6, f) == 6) {
		if (nev == cap) { cap = cap ? cap * 2 : 1024; ev = realloc(ev, cap * 6); }
		for (i = 0; i < 6; i++) ev[nev * 6 + i] = b[i];
		nev++;
	}
	fclose(f);
	n = (uint32_t)strtoul(argv[3], 0, 0);
	rate = (uint32_t)strtoul(argv[4], 0, 0);
	mask = (uint32_t)strtoul(argv[5], 0, 0);
	clock = (uint32_t)strtoul(argv[6], 0, 0);
	out = malloc((size_t)n * 2);
	opm_init(&chip, clock, rate, (uint8_t)mask);
	if (argc >= 9) latency = (uint32_t)strtoul(argv[8], 0, 0);
	if (argc >= 10) wsamp = (uint32_t)strtoul(argv[9], 0, 0);
	if (argc >= 8 && argv[7][0] != '-') {
		f = fopen(argv[7], "rb");
		if (!f || fread(steps, 4, 768, f) != 768) { perror(argv[7]); return 1; }
		fclose(f);
		chip.step_table = steps;
	}
	/* the chip takes one write per 64 clocks (busy flag), so a write lands at the later of
	 * its due time and WRITESAMPLES native samples after the write before it -- as jt51 and
	 * a driver space them; at a lower output rate several native samples pass per sample */
	per = rate ? (uint32_t)(((uint64_t)clock / 64 + rate - 1) / rate) : 1;
	for (i = 0; i < n; i++) {
		int32_t v;
		uint64_t now = (uint64_t)i * per;              /* native samples elapsed */
		while (e < nev) {
			const unsigned char *p = ev + e * 6;
			uint64_t t = (p[0] | p[1] << 8 | p[2] << 16 | (uint64_t)p[3] << 24) + latency;
			if (t < next_ok) t = next_ok;
			if (t >= now + per) break;
			opm_write(&chip, p[4], p[5]);
			next_ok = t + wsamp;
			e++;
		}
		v = opm_sample(&chip);
		out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
	}
	o = fopen(argv[2], "wb");
	if (!o) { perror(argv[2]); return 1; }
	fwrite(out, 2, n, o);
	fclose(o);
	return 0;
}
