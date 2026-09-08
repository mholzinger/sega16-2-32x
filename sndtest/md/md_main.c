/* sndtest 68K main — the sound-engine lab's MD side (docs/sound/SOUND.md P0).
 *
 * Trimmed from 32x-builder md_src/md_main.c (same author): the COMM0
 * command server, the busreq-park Z80 discipline, and the proven YM2612
 * write path survive; every SMS-session path is gone. This side will
 * grow the engine's 68K half (Z80 driver upload, VGM-stream feeder,
 * command router) in later phases; the game repo links the same engine
 * sources once they exist and are proven here.
 */
#include "common.h"
#include "z80_player.h"    // generated: player blob + ZP_* contract addrs
#include "test_track.h"    // generated: P3 streaming test stream
#include "sndmap_data.h"   // generated: arcade command map + per-cmd cuts
#include "render_music.h"  // generated: ALL music cmds rendered from the real driver

// 32X COMM
static volatile uint16_t* const mars_comm0  = (uint16_t*) MARS_COMM0;
static volatile uint16_t* const mars_comm2  = (uint16_t*) MARS_COMM2;
static volatile uint16_t* const mars_comm6  = (uint16_t*) MARS_COMM6;
static volatile uint16_t* const mars_comm8  = (uint16_t*) MARS_COMM8;
static volatile uint16_t* const mars_comm10 = (uint16_t*) MARS_COMM10;
static volatile uint16_t* const mars_comm12 = (uint16_t*) MARS_COMM12;

// VDP
static volatile uint16_t* const vdp_data_port = (uint16_t*) VDP_DATA_PORT;
static volatile uint16_t* const vdp_ctrl_port = (uint16_t*) VDP_CTRL_PORT;
static volatile uint32_t* const vdp_ctrl_wide = (uint32_t*) VDP_CTRL_PORT;

extern uint16_t read_joypad(uint8_t player);

uint16_t timer = 0;
uint16_t vramOffset = 0;

// Z80 BUSREQ-PARK (the 32x-builder law, verbatim). The Z80 must never
// free-run (it hammers the PSG with whatever it executes), but it also
// must never be parked IN RESET: the Z80 reset line resets the YM2612
// with it. Steady state is reset HIGH + bus GRANTED AND HELD: the CPU
// is frozen, the Yamaha lives. NON-static: read_joypad (md_start.s)
// tests this to keep its RESUME_Z80 from dissolving the park.
uint8_t z80_parked = 0;

// Sound-command router state — COMM6 publishes {count, last byte} so
// the menu can prove the round trip; dispatch lives in case 32.
static uint8_t snd_last_cmd = 0;
static uint8_t snd_cmd_count = 0;

// P3 music state: the Z80 runs the streaming player, the 68K feeds it
// 512B blocks once per vblank. mus_wr mirrors the produced-block
// counter (the Z80-RAM copy is the contract; this avoids a bus grab
// just to read our own number back).
static uint8_t  mus_on = 0;
static uint8_t  mus_wr = 0;
static uint32_t mus_pos = 0;
static const uint8_t *mus_src = test_track;
static uint32_t mus_len = TEST_TRACK_LEN;
static uint8_t  mus_loop = 1;
static uint8_t  speech_serial = 0;

// ---- LZSS streaming decompressor (tools/lzss.py format) --------------
// Full-length music is stored LZSS-compressed (~4x) so every track fits
// the 32X 512KB 68K ROM window without banking (docs/sound/SOUND_DRIVER.md). The
// feeder decompresses straight into the Z80 ring; a 4KB circular window
// holds recent output for back-references. mus_compressed selects this
// path; the legacy raw path (test_track) stays for the arpeggio.
static uint8_t  mus_compressed = 0;
static const uint8_t *lz_src;
static uint32_t lz_len, lz_pos;
static uint8_t  lz_bits;
static uint16_t lz_flags, lz_wpos, lz_msrc, lz_mlen;
static uint8_t  lz_win[4096];
// intro/loop: each VGM song is stored as an INTRO stream + a LOOP
// stream (split at the VGM loop point). Play the intro once, then loop
// the loop section forever — the authentic arcade behaviour.
static const uint8_t *lz_loopsrc;
static uint32_t lz_looplen;
static uint8_t  lz_phase;               // 0 = intro, 1 = looping section

static void lz_reset(void) {
	lz_pos = 0; lz_bits = 0; lz_flags = 0; lz_wpos = 0; lz_mlen = 0;
}

static uint8_t lz_next(void) {
	if (lz_mlen) {                         // continue an in-flight match
		uint8_t b = lz_win[lz_msrc & 4095];
		lz_msrc++; lz_mlen--;
		lz_win[lz_wpos & 4095] = b; lz_wpos++;
		return b;
	}
	if (lz_pos >= lz_len) {                // stream end
		if (!mus_loop) return 0xFF;
		if (lz_phase == 0) {               // intro done -> switch to loop
			lz_phase = 1;
			lz_src = lz_loopsrc; lz_len = lz_looplen;
		}
		if (lz_len == 0) return 0xFF;
		lz_reset();                        // (re)start the loop section
	}
	if (lz_bits == 0) { lz_flags = lz_src[lz_pos++]; lz_bits = 8; }
	uint8_t ism = lz_flags & 1; lz_flags >>= 1; lz_bits--;
	if (ism) {
		uint16_t tok = ((uint16_t)lz_src[lz_pos] << 8) | lz_src[lz_pos + 1];
		lz_pos += 2;
		lz_mlen = (uint16_t)((tok & 0x0F) + 3);
		lz_msrc = (uint16_t)(lz_wpos - ((tok >> 4) + 1));
		uint8_t b = lz_win[lz_msrc & 4095];
		lz_msrc++; lz_mlen--;
		lz_win[lz_wpos & 4095] = b; lz_wpos++;
		return b;
	}
	uint8_t b = lz_src[lz_pos++];
	lz_win[lz_wpos & 4095] = b; lz_wpos++;
	return b;
}

// ---- Channel VU tracker (visual aid for balancing the mix) ----------
// As the feeder decompresses each block it parses the Z80 opcodes
// (F0/F1 = YM2612 part I/II write [reg,val]; F2 = PSG [val]; <0x80 =
// wait) and tracks per channel: key-on state + carrier level. 6 FM
// (ch0-5) + 2 PSG (ch6-7). A snapshot is taken per block; music_feed
// publishes the snapshot of the block the Z80 is CURRENTLY reading, so
// the bars are synced to what is audible (block ~0.6s), not to the 4KB
// decompression lead. COMM10 = ch0-3 nibbles, COMM2 = ch4-7 nibbles.
// ---- Live mixer (Mike 2026-09-03: adjust/mute instruments WHILE the
// song plays). The SH-2 sends per-channel {offset, mute} (case 34);
// the feeder applies them to the CARRIER TL writes (and PSG volume
// latches) as the bytes stream into the ring, so no Z80 change and the
// VU shows the adjusted level. Carriers per YM2612 algorithm as a mask
// over register slot index (reg 0x40 + 4*slot + ch: 0=S1 1=S3 2=S2 3=S4):
// alg 0-3 = S4; 4 = S2+S4; 5,6 = S2+S3+S4; 7 = all.
static int8_t   mix_off[8];                // TL units, + = quieter
static uint8_t  mix_mute[8];
// The rips set most TLs ONCE at song start, so a mid-song change must
// be re-emitted: vu_byte records each channel's raw (pre-mixer) TL per
// slot; a change marks the channel pending and fill_block injects fresh
// carrier-TL writes (through vu_byte, so the offset applies) at the
// head of the next block — audible within ~1s.
static uint8_t  vu_tlbase[6][4];           // raw TL per FM ch, slot 0..3
static uint8_t  vu_psgbase[2];             // raw PSG volume latch att
static uint8_t  mix_pending;               // bit per channel
static uint8_t  mix_q[80], mix_qlen, mix_qpos;
static uint8_t  vu_alg[6];                 // per FM channel, from 0xB0-0xB2
static const uint8_t CARRIER_MASK[8] = { 0x8, 0x8, 0x8, 0x8, 0xC, 0xE, 0xE, 0xF };
static uint8_t  vu_key[8], vu_tl[8];      // key-on, carrier TL / psg att
static uint8_t  vu_hit[8];                 // key-on happened in this window
static uint8_t  vu_state, vu_reg, vu_part; // opcode parser
static uint16_t vu_ms;                     // cumulative wait-ms within block
// Sub-block timing: 8 snapshots per 512B block (one per 64B), each
// tagged with the cumulative wait-ms at its end. At publish time the
// feeder picks the window that is playing NOW from the frames elapsed
// since the Z80 advanced to this block (~16.7ms/frame). Waits are the
// real timeline, so this tracks individual notes (~75ms), not 0.6s.
static uint32_t vu_sub[8][8];              // [block][window] packed levels
static uint16_t vu_subms[8][8];            // [block][window] end time, ms
// Envelope model (Mike 2026-09-02: "drain when not actively emitting,
// not full while the channel is merely keyed"). Per FM channel the
// carrier's D1R/D2R/D1L/RR come through the same stream; the tracker
// advances an attenuation `vu_ea` (0..127, TL units) over each window's
// elapsed ms: key-on -> 0, decay1 at D1R to the D1L sustain, decay2 at
// D2R, release at RR on key-off. Level = TL + ea, so a pluck drains
// while still keyed and a pad holds until its release.
static uint8_t  vu_d1r[6], vu_d2r[6], vu_d1l[6], vu_rr[6];
static uint8_t  vu_ea[6], vu_ph[6];        // attenuation; 0 rel,1 dec1,2 dec2
static uint16_t vu_ms_last;                // window start, ms
// full-scale (0->127) decay ~60000ms / 2^(r/2); att += (dt_ms*inc)>>8
static const uint16_t eg_inc[32] = {
	    0,     1,     1,     2,     2,     3,     4,     6,
	    9,    12,    17,    25,    35,    49,    69,    98,
	  139,   196,   277,   392,   555,   785,  1110,  1569,
	 2219,  3139,  4439,  6278,  8878, 12555, 17756, 25111,
};

// Parses one stream byte; returns it, possibly MODIFIED by the mixer.
static uint8_t vu_byte(uint8_t b) {
	switch (vu_state) {
	case 0:
		if (b < 0x80) { vu_ms += b; }          // wait opcode = ms
		else if (b == 0xF0) { vu_state = 1; vu_part = 0; }
		else if (b == 0xF1) { vu_state = 1; vu_part = 3; }
		else if (b == 0xF2) { vu_state = 3; }
		return b;
	case 1: vu_reg = b; vu_state = 2; return b;
	case 2:
		vu_state = 0;
		if (vu_reg >= 0x40 && vu_reg <= 0x4E) {          // any operator TL
			uint8_t c = (uint8_t)(vu_part + (vu_reg & 3));
			uint8_t slot = (uint8_t)((vu_reg - 0x40) >> 2);
			if (c < 6) vu_tlbase[c][slot] = b & 0x7F;    // raw, pre-mixer
			if (c < 6 && (CARRIER_MASK[vu_alg[c]] & (1 << slot))) {
				int16_t tl = (int16_t)(b & 0x7F) + mix_off[c];
				if (mix_mute[c]) tl = 127;
				if (tl < 0) tl = 0; else if (tl > 127) tl = 127;
				b = (uint8_t)((b & 0x80) | tl);
			}
		} else if (vu_reg >= 0xB0 && vu_reg <= 0xB2) {   // algorithm
			vu_alg[vu_part + (vu_reg - 0xB0)] = b & 7;
		}
		if (vu_reg == 0x28) {                  // key on/off (part I reg)
			uint8_t ch = b & 7;
			if (ch >= 4) ch -= 1;              // 4,5,6 -> 3,4,5
			if (ch < 6) {
				vu_key[ch] = (b & 0xF0) ? 1 : 0;
				if (b & 0xF0) {                // note struck: envelope restarts
					vu_hit[ch] = 1;
					vu_ea[ch] = 0; vu_ph[ch] = 1;
				} else {
					vu_ph[ch] = 0;             // key-off: release
				}
			}
		} else if (vu_reg >= 0x4C && vu_reg <= 0x4E) {  // slot-3 carrier TL
			vu_tl[vu_part + (vu_reg - 0x4C)] = b & 0x7F;
		} else if (vu_reg >= 0x6C && vu_reg <= 0x6E) {  // carrier D1R
			vu_d1r[vu_part + (vu_reg - 0x6C)] = b & 0x1F;
		} else if (vu_reg >= 0x7C && vu_reg <= 0x7E) {  // carrier D2R
			vu_d2r[vu_part + (vu_reg - 0x7C)] = b & 0x1F;
		} else if (vu_reg >= 0x8C && vu_reg <= 0x8E) {  // carrier D1L | RR
			uint8_t c = vu_part + (vu_reg - 0x8C);
			vu_d1l[c] = b >> 4; vu_rr[c] = b & 0x0F;
		}
		return b;
	case 3:                                    // PSG data byte
		vu_state = 0;
		if ((b & 0x90) == 0x90) {              // volume latch
			uint8_t tone = (b >> 5) & 3, att = b & 0x0F;
			if (tone < 2) {
				// mixer: TL units -> 2dB steps (x3/8); mute = 15
				vu_psgbase[tone] = att;                  // raw, pre-mixer
				int16_t a = att;
				if (att < 15) a += (mix_off[6 + tone] * 3) >> 3;
				if (mix_mute[6 + tone]) a = 15;
				if (a < 0) a = 0; else if (a > 15) a = 15;
				att = (uint8_t)a;
				b = (uint8_t)(0x90 | (tone << 5) | att);
				vu_tl[6 + tone] = att; vu_key[6 + tone] = (att < 15);
				if (att < 15) vu_hit[6 + tone] = 1;
			}
		}
		return b;
	}
	return b;
}

static void vu_subsnap(uint8_t blk, uint8_t sub) {
	uint32_t p = 0;
	uint16_t dt = (uint16_t)(vu_ms - vu_ms_last);   // this window's ms
	vu_ms_last = vu_ms;
	for (uint8_t c = 0; c < 8; c++) {
		uint8_t lvl = 0;
		if (c < 6) {
			// advance the carrier envelope over dt
			uint8_t r = 0, cap = 127;
			if (vu_ph[c] == 1) {               // decay1 toward D1L
				r = vu_d1r[c];
				cap = (uint8_t)(vu_d1l[c] == 15 ? 127 : vu_d1l[c] * 4);
			} else if (vu_ph[c] == 2) {        // decay2 (sustain slope)
				r = vu_d2r[c];
			} else {                           // release
				r = (uint8_t)(vu_rr[c] * 2 + 1);
			}
			uint32_t step = ((uint32_t)dt * eg_inc[r]) >> 8;
			uint32_t ea = (uint32_t)vu_ea[c] + step;
			if (ea >= cap) { ea = cap; if (vu_ph[c] == 1) vu_ph[c] = 2; }
			vu_ea[c] = (uint8_t)ea;
			// Ceiling = this channel's own peak (its TL, absolute — so
			// bar length still reads as BALANCE between channels);
			// floor = 48 att units (-36dB) below that peak, i.e. audibly
			// silent -> flat. The old floor (-90dB) left a long lit-but-
			// inaudible tail (Mike, 2026-09-02).
			uint8_t a = (uint8_t)(vu_tl[c] >> 3);
			uint8_t peak = (uint8_t)(15 - (a > 15 ? 15 : a));
			uint8_t drain = vu_hit[c] ? 0 : (vu_ea[c] > 48 ? 48 : vu_ea[c]);
			lvl = (uint8_t)(peak - ((uint16_t)peak * drain) / 48);
			if (drain >= 48) lvl = 0;
		} else if (vu_hit[c] || vu_key[c]) {   // PSG: latch until key-off
			uint8_t a = vu_tl[c] > 15 ? 15 : vu_tl[c];
			lvl = (uint8_t)(15 - a);
			if (lvl == 0) lvl = 1;
		}
		vu_hit[c] = 0;
		p |= (uint32_t)lvl << (c * 4);
	}
	vu_sub[blk & 7][sub & 7] = p;
	vu_subms[blk & 7][sub & 7] = vu_ms;
}

// Fill one 512B ring block from the current source. Looping tracks
// wrap; one-shots pad the tail with 0xFF end opcodes — the Z80 player
// hits the first one, reports MSTAT=2, and the feeder auto-stops.
static void fill_block(volatile uint8_t *zram, uint16_t base, uint8_t blk) {
	vu_ms = 0; vu_ms_last = 0;
	// Live mixer: queue fresh writes for changed channels at the head of
	// this block. Raw base values go through vu_byte like any stream
	// byte, so the new offset/mute applies and the VU tracks it.
	mix_qlen = mix_qpos = 0;
	if (mix_pending) {
		for (uint8_t c = 0; c < 8; c++) {
			if (!(mix_pending & (1 << c))) continue;
			if (c < 6) {
				uint8_t part = (uint8_t)(c >= 3), ofs = (uint8_t)(c % 3);
				for (uint8_t s = 0; s < 4; s++) {
					if (!(CARRIER_MASK[vu_alg[c]] & (1 << s))) continue;
					mix_q[mix_qlen++] = part ? 0xF1 : 0xF0;
					mix_q[mix_qlen++] = (uint8_t)(0x40 + 4 * s + ofs);
					mix_q[mix_qlen++] = vu_tlbase[c][s];
				}
			} else {
				uint8_t tone = (uint8_t)(c - 6);
				mix_q[mix_qlen++] = 0xF2;
				mix_q[mix_qlen++] = (uint8_t)(0x90 | (tone << 5) | vu_psgbase[tone]);
			}
		}
		mix_pending = 0;
	}
	for (uint16_t i = 0; i < 512; i++) {
		uint8_t b;
		if (mix_qpos < mix_qlen) {
			b = mix_q[mix_qpos++];
		} else if (mus_compressed) {
			b = lz_next();
		} else if (mus_pos < mus_len) {
			b = mus_src[mus_pos++];
		} else if (mus_loop) {
			mus_pos = 0;
			b = mus_src[mus_pos++];
		} else {
			b = 0xFF;
		}
		b = vu_byte(b);                         // VU tracking + live mixer
		zram[base + i] = b;
		if ((i & 63) == 63)                     // 8 windows per block
			vu_subsnap(blk, (uint8_t)(i >> 6));
	}
}

// One YM2612 part-I write with the proven busy+settle pacing, for use
// while the 68K already OWNS the Z80 bus (stop-path cleanup).
static void ym_write_held(uint8_t reg, uint8_t val) {
	volatile uint8_t *ym = (uint8_t *)0xA04000;
	uint32_t g = 200; while ((ym[0] & 0x80) && --g) ;
	ym[0] = reg;
	for (volatile uint16_t d = 0; d < 8; d++) ;
	g = 200; while ((ym[0] & 0x80) && --g) ;
	ym[1] = val;
	for (volatile uint16_t d = 0; d < 32; d++) ;
}

// Start a track on the Z80 player: the proven session-boot dance
// (case-12 lineage): release reset FIRST, request bus, upload the
// player, zero the mailboxes (uninit Z80 RAM boots 0xFF — the
// phantom-command lesson), PRIME the full 8-block ring, set MGO,
// reset pulse while owning the bus, hand the bus back. The Z80 wakes
// at $0000, sees MGO, and owns the YM2612+PSG from then on.
static void music_start_ex(const uint8_t *src, uint32_t len, uint8_t loop,
                           uint8_t compressed, const uint8_t *loopsrc,
                           uint32_t looplen) {
	volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
	volatile uint16_t *reset  = (uint16_t *)Z80_RESET;
	volatile uint8_t  *zram   = (uint8_t *)0xA00000;
	mus_compressed = compressed;
	if (compressed) {
		lz_src = src; lz_len = len;
		lz_loopsrc = loopsrc; lz_looplen = looplen;
		lz_phase = (loopsrc == src) ? 1 : 0;  // 1 = single-stream loop
		lz_reset();
	} else {
		mus_src = src; mus_len = len;
	}
	mus_loop = loop;
	*reset = 0x100;
	*busreq = 0x100;
	{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
	for (uint16_t i = 0; i < Z80_PLAYER_LEN; i++)
		zram[i] = z80_player[i];
	zram[ZP_RD_BLK] = 0;
	zram[ZP_WR_BLK] = 0;
	zram[ZP_MSTAT]  = 0;
	zram[ZP_MGO]    = 0;
	mus_pos = 0;
	for (uint8_t c = 0; c < 8; c++) { vu_key[c] = 0; vu_tl[c] = 127; vu_hit[c] = 0; }
	for (uint8_t c = 0; c < 6; c++) {
		vu_ea[c] = 127; vu_ph[c] = 0;
		for (uint8_t s = 0; s < 4; s++) vu_tlbase[c][s] = 127;
	}
	vu_psgbase[0] = vu_psgbase[1] = 15;
	mix_pending = 0; mix_qlen = mix_qpos = 0;   // offsets/mutes persist
	vu_state = 0;
	// Prime 3 blocks (~1.8s), not 8: with the 2-ahead top-up the feeder
	// otherwise stays idle for the first ~5s and a live mixer change
	// made then would wait that long to be injected (2026-09-03).
	for (mus_wr = 0; mus_wr < 3; mus_wr++)
		fill_block(zram, (uint16_t)(ZP_RING + ((uint16_t)mus_wr << 9)), mus_wr);
	zram[ZP_WR_BLK] = mus_wr;
	zram[ZP_MGO] = 1;
	*reset = 0x000;                    // pulse while we own the bus
	for (volatile uint16_t d = 0; d < 64; d++) ;
	*busreq = 0x000;                   // hand the bus back...
	*reset = 0x100;                    // ...and let the Z80 run
	z80_parked = 0;                    // the player owns the bus now
	mus_on = 1;
}

static void music_start(const uint8_t *src, uint32_t len, uint8_t loop) {
	music_start_ex(src, len, loop, 0, src, len);   // raw (test_track path)
}

// Start a VGM song: play the INTRO stream once, then loop the LOOP
// stream. Both LZSS-compressed; the feeder stream-decompresses. The
// intro/loop state is set before the ring is primed (inside _ex).
static void music_start_vgm(const uint8_t *intro, uint32_t ilen,
                            const uint8_t *loop, uint32_t llen) {
	music_start_ex(intro, ilen, 1, 1, loop, llen);
}

static void music_stop(void) {
	volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
	mus_on = 0;
	*busreq = 0x100;                   // busreq-park: reset stays HIGH,
	{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
	z80_parked = 1;                    // the YM keeps its registers
	// key off all six channels
	ym_write_held(0x28, 0x00); ym_write_held(0x28, 0x01);
	ym_write_held(0x28, 0x02); ym_write_held(0x28, 0x04);
	ym_write_held(0x28, 0x05); ym_write_held(0x28, 0x06);
	{	// PSG died mid-note: attenuation 15 on all channels
		volatile uint8_t *psg = (uint8_t *)0xC00011;
		*psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF;
	}
}

__attribute__((section(".data")))
void vdp_color(uint16_t index, uint16_t color) {
	index <<= 1;
	*vdp_ctrl_wide = ((0xC000 + (((uint32_t)index) & 0x3FFF)) << 16) + (((uint32_t)index) >> 14);
	*vdp_data_port = color;
}

__attribute__((section(".data")))
void do_commands(void) {
	uint16_t cmd = *mars_comm0;
	switch(cmd >> 8) {
	default: break; // Unknown command
	case 0: return; // No command
	case 3:
		*mars_comm8 = read_joypad(cmd);
		break;
	case 4: { // CLEAR SCREEN: sweep all of Name Table B (name table only —
		// the font in VRAM is never touched).
		for (uint16_t row = 0; row < 28; row++) {
			uint32_t ofs = (row * 64u * 2u) + 0xE000;
			*vdp_ctrl_wide = (uint32_t)(0x4000 | (ofs & 0x3FFF)) << 16
			               | ((ofs >> 14) | 0x03);
			for (uint16_t i = 0; i < 40; i++)
				*vdp_data_port = 0;
		}
		break;
	}
	case 5: // Set VRAM or Plane offset
		// Mask so the control word 0x6000+ofs can never reach 0x8000 (a
		// VDP REGISTER write) — a corrupt parameter did exactly that on
		// 2026-09-02 (plane-size / auto-increment garbage).
		vramOffset = (uint16_t)(*mars_comm2 & 0x1FFF);
		break;
	case 6: // Write tile to Plane B
		*vdp_ctrl_wide = (((uint32_t)0x6000 + ((vramOffset) & 0x3FFF)) << 16) + (((vramOffset) >> 14) | 0x03);
		*vdp_data_port = *mars_comm2;
		vramOffset += 2;
		break;
	case 7: // Write word to VRAM address
		*vdp_ctrl_wide = (((uint32_t)0x4000 + ((vramOffset) & 0x3FFF)) << 16) + (((vramOffset) >> 14) | 0x00);
		*vdp_data_port = *mars_comm2;
		vramOffset += 2;
		break;
	case 8: // Set MD CRAM color: index in cmd low byte, BGR word in COMM2.
		vdp_color(cmd & 0xFF, *mars_comm2);
		break;
	case 14: { // YM2612 part-I register write: reg in cmd low byte, value
		// in COMM2. Proven path from 32x-builder — busreq for the
		// duration, busy-poll + settle delays, canaries on failure.
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		volatile uint8_t  *ym     = (uint8_t *)0xA04000;
		uint16_t val = *mars_comm2;
		// CANARY (backdrop color names the failure in a screenshot):
		// RED = Z80 bus grant timed out. BLUE = grant OK but the YM busy
		// flag never cleared ($A04000 reads garbage).
		// ORDER IS LAW: no bus grant while the Z80 is held in reset.
		// Release reset FIRST, then request; re-park on the way out.
		volatile uint16_t *zreset = (uint16_t *)Z80_RESET;
		*zreset = 0x100;
		*busreq = 0x100;
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ;
		  if (!g) vdp_color(1, 0x00E); }
		{ uint32_t g = 200; while ((ym[0] & 0x80) && --g) ;
		  if (!g) vdp_color(1, 0xE00); }
		ym[0] = (uint8_t)(cmd & 0xFF);       // address port
		for (volatile uint16_t d = 0; d < 8; d++) ;
		{ uint32_t g = 200; while ((ym[0] & 0x80) && --g) ; }
		ym[1] = (uint8_t)val;                // data port
		for (volatile uint16_t d = 0; d < 32; d++) ;
		{ uint32_t g = 200; while ((ym[0] & 0x80) && --g) ; }
		// The held grant IS the park; only a live Z80 session (none in
		// P0) releases the bus. Reset stays high, the YM keeps state.
		if (!z80_parked) *busreq = 0x000;
		break;
	}
	case 15: { // YM smoke test, ONE command per action (frame-spaced
		// per-register writes are the PROVEN-sounding shape; bursts of
		// register writes inside ONE command still land because YMW
		// paces with busy-poll + settle delays — ~2ms for the patch).
		// op: 0 = all off, 1 = upload test patch + key on, 2 = retrigger,
		// 3 = release. The patch is 32x-builder's two-voice drone —
		// known-audible on hardware, which is the whole point of a smoke
		// test: any silence is OUR bug.
		static const uint8_t test_patch[][2] = {
			{0x22, 0x00}, {0x27, 0x00}, {0x2B, 0x00},
			// ch1
			{0x30, 0x01}, {0x34, 0x03}, {0x38, 0x02}, {0x3C, 0x14},
			{0x40, 0x28}, {0x44, 0x34}, {0x48, 0x20}, {0x4C, 0x3A},
			{0x50, 0x1F}, {0x54, 0x1F}, {0x58, 0x1F}, {0x5C, 0x1F},
			{0x60, 0x80}, {0x64, 0x80}, {0x68, 0x80}, {0x6C, 0x80},
			{0x70, 0x00}, {0x74, 0x00}, {0x78, 0x00}, {0x7C, 0x00},
			{0x80, 0x06}, {0x84, 0x06}, {0x88, 0x06}, {0x8C, 0x06},
			{0x90, 0x00}, {0x94, 0x00}, {0x98, 0x00}, {0x9C, 0x00},
			{0xB0, 0x2F}, {0xB4, 0xD1}, {0xA4, 0x0C}, {0xA0, 0x9D},
			// ch2
			{0x31, 0x01}, {0x35, 0x03}, {0x39, 0x02}, {0x3D, 0x34},
			{0x41, 0x30}, {0x45, 0x3C}, {0x49, 0x28}, {0x4D, 0x42},
			{0x51, 0x1F}, {0x55, 0x1F}, {0x59, 0x1F}, {0x5D, 0x1F},
			{0x61, 0x80}, {0x65, 0x80}, {0x69, 0x80}, {0x6D, 0x80},
			{0x71, 0x00}, {0x75, 0x00}, {0x79, 0x00}, {0x7D, 0x00},
			{0x81, 0x08}, {0x85, 0x08}, {0x89, 0x08}, {0x8D, 0x08},
			{0x91, 0x00}, {0x95, 0x00}, {0x99, 0x00}, {0x9D, 0x00},
			{0xB1, 0x3F}, {0xB5, 0xE1}, {0xA5, 0x0C}, {0xA1, 0x9D},
		};
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		volatile uint16_t *zreset = (uint16_t *)Z80_RESET;
		volatile uint8_t  *ym     = (uint8_t *)0xA04000;
		uint16_t op = cmd & 0xFF;
		*zreset = 0x100;                     // no grant while reset (LAW)
		*busreq = 0x100;
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
		#define YMW(r, v) do { \
			uint32_t g = 200; while ((ym[0] & 0x80) && --g) ; \
			ym[0] = (r); \
			for (volatile uint16_t d = 0; d < 8; d++) ; \
			g = 200; while ((ym[0] & 0x80) && --g) ; \
			ym[1] = (v); \
			for (volatile uint16_t d = 0; d < 32; d++) ; } while (0)
		switch (op) {
		case 0: YMW(0x28, 0x00); YMW(0x28, 0x01); break;
		case 1:
			for (uint16_t i = 0; i < sizeof(test_patch) / 2; i++)
				YMW(test_patch[i][0], test_patch[i][1]);
			YMW(0x28, 0xF1);                       // key on (ch2)
			break;
		case 2: YMW(0x28, 0x00); YMW(0x28, 0xF0); break;
		case 3: YMW(0x28, 0x00); break;
		}
		#undef YMW
		{ uint32_t g = 200; while ((ym[0] & 0x80) && --g) ; }
		if (!z80_parked) *busreq = 0x000;
		break;
	}
	case 33: { // MUSIC (0x21xx): op 1 = test arpeggio, op 2 = round-1
		// BGM via the command map (0x94's track), op 0 = stop.
		if ((cmd & 3) == 2) {
			const struct snd_ent *e = &sndmap[0x94];
			music_start(snd_trks[e->idx].p, snd_trks[e->idx].len,
			            snd_trks[e->idx].loop);
		} else if (cmd & 3)
			music_start(test_track, TEST_TRACK_LEN, 1);
		else
			music_stop();
		break;
	}
	case 34: { // MIX (0x22xx): live per-channel level for the sound
		// test's mixer mode. low byte = channel 0-7; COMM2 = {mute<<8 |
		// (int8)offset} in TL units (0.75dB, + = quieter). Applied by
		// the feeder to carrier TL / PSG volume writes as they stream.
		uint8_t c = (uint8_t)(cmd & 7);
		uint16_t v = *mars_comm2;
		mix_off[c]  = (int8_t)(v & 0xFF);
		mix_mute[c] = (uint8_t)((v >> 8) & 1);
		mix_pending |= (uint8_t)(1 << c);     // re-emit its TL next block
		break;
	}
	case 32: { // SOUND COMMAND (0x20xx): the engine's front door — the
		// low byte is an arcade sound-command byte, exactly what the
		// game's MCU mailbox carries. REAL DISPATCH via the generated
		// command map (soundmap_build.py): music/ymsfx -> Z80 player
		// (one-shots auto-stop via MSTAT), speech -> SH-2 PCM lane
		// through the COMM4 doorbell (serial<<8 | bank idx).
		uint8_t b = (uint8_t)(cmd & 0xFF);
		snd_last_cmd = b;
		snd_cmd_count++;
		const struct rtrk *rt = 0;
		for (int ri = 0; ri < RENDER_MUSIC_COUNT; ri++)
			if (render_music[ri].cmd == b) { rt = &render_music[ri]; break; }
		if (b == 0x00) {
			music_stop();          // arcade stop convention
		} else if (rt) {
			// FAITHFUL music (2026-09-01): every music command plays the
			// full-length render from the REAL arcade driver, LZSS-
			// compressed (docs/sound/SOUND_DRIVER.md, render_music.h) and stream-
			// decompressed into the ring. Replaces the isolation-sweep
			// trk_XX that faded (the 0x01=volume-1 latch bug).
			music_start_vgm(rt->intro, rt->intro_len,
			                rt->loop, rt->loop_len);
		} else {
			const struct snd_ent *e = &sndmap[b];
			if (e->type == SND_MUSIC || e->type == SND_YMSFX)
				music_start(snd_trks[e->idx].p, snd_trks[e->idx].len,
				            snd_trks[e->idx].loop);
			if (e->sp != 0xFF) {   // PCM component (speech or MIXED sfx)
				speech_serial = (uint8_t)((speech_serial + 1) & 0x3F);
				*((volatile uint16_t *)MARS_COMM4) =
					(uint16_t)(((uint16_t)speech_serial << 8) | e->sp);
			}
		}
		*mars_comm6 = (uint16_t)(((uint16_t)snd_cmd_count << 8) | snd_last_cmd);
		break;
	}
	}
	*mars_comm0 = 0;
}

// P3 feeder: once per vblank, a short busreq window to read the Z80's
// consumed-block counter and top the 4KB ring up (at most 2 blocks per
// frame — the stream burns ~1 block/s, so this recovers from any stall
// fast without hogging the bus). The source wraps forever: the loop IS
// the wrap, no restart protocol needed. COMM14 publishes {produced,
// consumed} so the SH-2 menu can watch the stream move.
__attribute__((section(".data")))
static void music_feed(void) {
	if (!mus_on) {
		// Stopped (B) or idle: keep publishing SILENCE so the SH-2 bars
		// drain instead of freezing at the last levels (Mike's screenshot,
		// 2026-09-02). Halves still alternate so both drain.
		static uint8_t zhalf;
		*mars_comm10 = (uint16_t)(zhalf ? 0x8000 : 0);
		zhalf ^= 1;
		return;
	}
	volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
	volatile uint8_t  *zram   = (uint8_t *)0xA00000;
	*busreq = 0x100;
	{ uint32_t g = 100000; while ((*busreq & 0x100) && --g) ; }
	uint8_t rd = zram[ZP_RD_BLK];
	uint8_t st = zram[ZP_MSTAT];
	uint8_t fed = 0;
	// Keep only 2 blocks (~1.2s) ahead so live mixer changes reach the
	// Z80 within ~1s (a block lasts ~0.6s; we refill every 16ms, so the
	// stall margin is still ~70 frames). The 8-block prime at start is
	// unchanged.
	while ((uint8_t)(mus_wr - rd) < 2 && fed < 2) {
		fill_block(zram, (uint16_t)(ZP_RING + (((uint16_t)mus_wr & 7) << 9)), mus_wr);
		mus_wr++;
		zram[ZP_WR_BLK] = mus_wr;
		fed++;
	}
	*busreq = 0x000;
	*((volatile uint16_t *)MARS_COMM14) =
		(uint16_t)(((uint16_t)mus_wr << 8) | rd);
	// VU: publish the window that is playing NOW. rd changing marks the
	// start of a block; frames since then (~16.7ms each) select the
	// sub-window by its cumulative wait-ms.
	{
		static uint8_t  vu_rd_prev = 0xFF;
		static uint16_t vu_frame, vu_rd_frame;
		vu_frame++;
		if (rd != vu_rd_prev) { vu_rd_prev = rd; vu_rd_frame = vu_frame; }
		uint16_t el = (uint16_t)((vu_frame - vu_rd_frame) * 17);
		uint8_t s = 0;
		while (s < 7 && vu_subms[rd & 7][s] < el) s++;
		uint32_t v = vu_sub[rd & 7][s];
		// Only COMM10 is ours. COMM2 is the SH-2's command PARAMETER
		// register — writing it here raced the HUD's set-offset/putc
		// commands and produced stray chars and VDP register writes
		// (every-other-row display, screenshots 51-57, 2026-09-02).
		// So: 4 channels per frame, halves alternate, bit 15 = half,
		// 3-bit levels (0..7) via LUT.
		static const uint8_t l4to3[16] =
			{ 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 7 };
		static uint8_t half;
		uint16_t w = (uint16_t)(half ? 0x8000 : 0);
		for (uint8_t c = 0; c < 4; c++) {
			uint8_t lvl = (uint8_t)((v >> ((half * 4 + c) * 4)) & 0xF);
			w |= (uint16_t)(l4to3[lvl] << (c * 3));
		}
		*mars_comm10 = w;
		half ^= 1;
	}
	if (st == 2)
		music_stop();          // one-shot hit its 0xFF end opcode
}

// Sticky six-button latch (32x-builder, verbatim): once a pad has EVER
// validated the six-button signature, a missed probe holds the last good
// extended bits instead of dropping MODE mid-hold.
__attribute__((section(".data")))
uint16_t pad_sticky(uint8_t n, uint16_t p) {
	static uint16_t last_ext[2];
	static uint8_t  is_six[2];
	if (p & 0x1000) {
		is_six[n] = 1;
		last_ext[n] = p & 0x0F00;
	} else if (is_six[n]) {
		p = (uint16_t)((p & ~0x0F00u) | last_ext[n] | 0x1000);
	}
	return p;
}

__attribute__((section(".data")))
void main(void) {
	// PARK THE Z80 AND THE PSG, in that order — busreq-park, not reset.
	// Request the bus first (pending — no grant comes while reset is
	// low), then release reset; the Z80 executes at most an instruction
	// or two of zeroed RAM before the grant freezes it, reset stays
	// high, the Yamaha lives, and the held grant IS the park.
	{
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		volatile uint16_t *reset  = (uint16_t *)Z80_RESET;
		*busreq = 0x100;                   // pending until reset releases
		*reset  = 0x100;
		{ uint32_t g = 200000; while ((*busreq & 0x100) && --g) ; }
		z80_parked = 1;                    // bus stays held: frozen, not reset
	}
	// PSG silent before anything else runs: attenuation 15, all four
	// channels. The PSG lives in the VDP and no Z80 reset touches it.
	{
		volatile uint8_t *psg = (uint8_t *)0xC00011;
		*psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF;
	}
	*mars_comm6 = 0;   // router ack starts clean (boot leaves residue here)
	while(1) {
		while(*vdp_ctrl_port & 8) do_commands();
		while(!(*vdp_ctrl_port & 8)) do_commands();
		// Publish both pads UNSOLICITED every frame; the SH-2 reads
		// COMM8/COMM10 directly with no request/response round-trip.
		*mars_comm8  = pad_sticky(0, read_joypad(0));
		/* COMM10 now carries the channel VU (music_feed); pad 2 is unused
		 * by the SH-2 menu, so it is no longer published. */
		*mars_comm12 = ++timer;
		music_feed();
	}
}
