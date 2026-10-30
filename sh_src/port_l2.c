/* #define PORT_FFWATCH 1 */  /* measurement hooks, off on the candidate */
void port_watch_hook(unsigned int tag);   /* measurement hook, defined below */
/* L2 DRIVER: run ONE FRAME of decompiled 68K game logic on the SH-2 and compare
 * the whole of work RAM against what the arcade held at the next frame.
 *
 * THE GATE (docs/design/FULL-PORT.md 10): L1 verifies one routine against captured
 * arguments. L2 verifies a FRAME -- seed all 16 KB, run the frame, diff all 16 KB --
 * so the first divergent cell names the routine that is wrong. A gameplay frame
 * changes only 40 of 16384 bytes, so the signal is sharp.
 *
 * WHY THIS IS A ROM AND NOT A HOST PROGRAM. The captures are big-endian 68K work
 * RAM. The host is little-endian arm64, so every multi-byte cell would be byte
 * swapped, and there is no per-cell width to swap by. The SH-2 is big-endian and
 * needs no conversion at all.
 *
 * THE FRAME BODY is not a function. It is an inline do/while inside the reset
 * routine (0x40E), identical in switch cases 0 and 8:
 *     FUN_00003aae(); FUN_0000144a(); object_dispatcher(); wait_frame_flag();
 * wait_frame_flag is the vsync barrier and is STUBBED (it decompiles to an
 * infinite loop, because the flag is set by IRQ4 which is outside the program).
 * irq4_handler runs first here, standing for the interrupt at the frame boundary.
 */
#ifdef PORT_FULL
#include "mars.h"
#include "sdram_map.h"
#include "game_cfg.h"

typedef unsigned int  uint;
typedef unsigned char byte;

/* THIS DRIVER RIDES THE CART, NOT .text. Measured 2026-09-30 and again 2026-10-01:
 * the low region sits under the 0x06019000 region guard AND .ramtext's ROM image is
 * within ~2 KB of .tilesmd at 0x26A000, so a couple of KB of new driver code breaks
 * the link from the other side ("`.ramtext` ROM image reached `.tilesmd`").
 * mars.ld's own note at line 40 says it: such an object "belongs in .portcode" --
 * the 448 KB window this build already owns (NOSPRBAKE=1), executed from the
 * cached cart view. */
#define PORTCODE __attribute__((section(".portcode"), noinline))
/* PORTHOT2 (2026-10-24): a runtime function the profile puts in the hot set
 * (tools/hot_set.txt, kind `runtime`) is compiled into SDRAM region A (.ramtext2, mars.ld)
 * instead of cart; the Makefile passes -DHOT2_<name> for each. One macro per function, so
 * the set is the file's and not this source's. */
#define PORTHOT2_SEC __attribute__((section(".ramtext2"), noinline))
#ifdef HOT2_port_walk_player_slots
#define SEC_port_walk_player_slots PORTHOT2_SEC
#else
#define SEC_port_walk_player_slots PORTCODE
#endif
#ifdef HOT2_port_despawn_index
#define SEC_port_despawn_index PORTHOT2_SEC
#else
#define SEC_port_despawn_index PORTCODE
#endif
#ifdef HOT2_port_call68_at
#define SEC_port_call68_at PORTHOT2_SEC
#else
#define SEC_port_call68_at PORTCODE
#endif
#ifdef HOT2_port_zoom_scale
#define SEC_port_zoom_scale PORTHOT2_SEC
#else
#define SEC_port_zoom_scale
#endif

extern char WRAM_BASE[];
extern char ROM_BASE[];
extern byte *_r_unaff_A6;          /* the 68K's A6: every handler's object pointer */
char port_spr_sink[16];            /* w68.h SPRREC: where a sprite record with index >= 0x80 goes (ROM on the 68K) */

extern uint irq4_handler(void);    /* 0x02AAC, the real VBL handler */
extern uint FUN_00003aae(void);
extern uint FUN_0000144a(void);
extern uint hide_object_sprite(void);

/* THE TABLE NOW TAKES THE REGISTER ARRAY. r[0..7] = D0..D7, r[8..14] = A0..A6;
 * every entry is a generated thunk that passes them in that handler's own declared
 * order (tools/cport_dispatch.py). It used to be unprototyped and called with no
 * arguments, which meant p_D6 read 0 -- and sprite_build_depth_banded gates
 * hide-vs-draw on p_D6, so NOTHING WAS EVER HIDDEN. FULL-PORT.md 26. */
struct port_handler { unsigned int a68; uint (*fn)(const uint *r); };
extern const struct port_handler port_handlers[];
extern const unsigned int port_handlers_n;

/* the live register file, filled by the dispatcher and shared with port_call68_at
 * (an indirect call has no slot, so it inherits whatever the dispatch set) */
static uint port_cur_regs[15];
void port_set_reg(unsigned int ix, unsigned int v);

/* THE REGISTER FILE the ported code writes back into. A function whose body assigns
 * a register PARAMETER stores it here before returning (tools/cport_prep.py), which
 * is how a modification survives the return -- the 68000 threaded these between
 * routines and a by-value C parameter cannot. FULL-PORT.md 30. */
unsigned int _port_reg[15];

/* SET A LIVE REGISTER BEFORE AN INDIRECT CALL.
 *
 * TWO ARRAYS, and confusing them cost a round of measurement. `port_cur_regs` is
 * what the dispatch THUNKS read; `_port_reg` is what the ported code writes BACK
 * into on return. They are not the same file, and a pass that published an
 * A-register into `_port_reg` before an indirect call changed nothing at all --
 * the value was correct (A5 = 0x0602EC00 = 68K 0xFFE000, verified) and simply not
 * where the callee looks.
 *
 * So the generated unit calls this instead. It writes the LIVE file, which is
 * exactly what the existing contract already says happens: "an indirect call has no
 * slot, so it inherits whatever the dispatch set". Inheriting what the CALLER
 * computed is strictly more faithful than inheriting the last handler's leftovers --
 * the 68000 really does carry A5 across a `jsr`.
 *
 * Why not copy `_port_reg` wholesale in port_call68_at: that would also overwrite
 * A6 (which the dispatcher owns and must keep per-object) and the D3/D6 pair that is
 * deliberately THREADED handler to handler. Only the registers a caller has been
 * proven to hold may move, and tools/cport_prep.py decides that from Ghidra's own
 * variable storage. */
void port_set_reg(unsigned int ix, unsigned int v)
{
    if (ix < 15u) port_cur_regs[ix] = v;
}
unsigned int _port_reg_n;   /* how many write-backs actually ran */
uint port_zoom_dbg[3];      /* port_zoom_scale: last D3, call count, last 68K A0 */
/* port_spawn_walk's own view: [0] table base, [1] cursor in, [2] camera high half,
 * [3] the first trigger, [4] slots claimed, [5] loop passes. Its own array for the
 * same reason port_zoom_dbg has one -- referencing port_l2m_out from PORTCODE puts
 * that array in .portcode and gcc refuses the section conflict. */
uint port_spawn_dbg[6];
/* port_spawn_a6 (the record FUN_0000e0f4 allocated) is DEFINED by the generated defs.c: cport_prep.py collects every undeclared identifier the unit uses. Defining it here too was rd44's duplicate-symbol link error. */
#ifdef PORT_DISPREGS
extern const unsigned char port_dispregs_have[64];
extern const unsigned int port_dispregs[64][15];
unsigned int port_dispreg_fix(unsigned int i, unsigned int v);
#endif

extern const byte l2_before[];     /* work RAM at the seed frame */
/* NO l2_after. It is EXACTLY the seed with delta frame 1 applied, so carrying a
 * second 16 KB image cost .portdata space the delta chain needed; the one-frame
 * ladder scores through l2m_score(w, 1, ...) like every other frame. */

#define WRAM_BYTES 16384u          /* 68K 0xFFC000-0xFFFFFF */
#define SLOTS      64u             /* object_dispatcher: 0x40 slots */
#define STRIDE     0x80u

/* CANARIES EITHER SIDE OF THE RESULT ARRAY. The port runs with garbage pointers in
 * it -- 272 indirect calls in one frame resolve to addresses that are not in the
 * handler table, and an untranslated 68K address can land anywhere in SDRAM.
 * Measured 2026-09-28: port_l2_out[12] read 93,168 handler RETURNS against 40
 * entries, which is impossible, so something had written through the array. A
 * result that cannot prove it was not overwritten is not a measurement. */
/* PORT-SIDE CALL TRACE, to diff against the arcade's own.
 *
 * The game writes a progress marker to 0xFFFEF4/F6 before each call it makes, and
 * tools/l1_oracle.lua captures it -- so the arcade's exact path through a handler
 * is known: FUN_0000ee80+6 (restore_position), then straight to +194 (the state
 * dispatch), then a ten-call tail. The port takes a different path, and until now
 * the only way to find out where was to read the control flow by hand.
 *
 * Ghidra DROPPED those marker writes (dead stores from its point of view), so the
 * port cannot reproduce them. -finstrument-functions gives the same information
 * from the other side: gcc calls this on entry to every function in the port unit,
 * and `this_fn` symbolizes against rom/s16.lst.
 *
 * A RING, not a log: a frame makes thousands of calls and only the divergence
 * matters. Recording from the moment a chosen handler is entered gives the first
 * N calls of exactly the routine under study. */
#define TRACE_N 128
volatile unsigned int port_trace[TRACE_N];
volatile unsigned int port_trace_n;
static int trace_arm;
/* which handler to trace; 0 = none */
volatile unsigned int port_trace_want = 0x0EE80u;

static void *probe_fn;
PORTCODE static void probe_sample(void);
#define WATCH_DEPTH 96u
/* THE MAP OF THIS ARRAY, WRITTEN DOWN BECAUSE I COLLIDED IN IT. The hit counts
 * went to [60..63], which is inside the per-slot block at [32..63], and read back
 * 4312 -- a slot FIELD value. That is the fifth slot collision in this project's
 * result arrays (HANDOFF 5 trap 8 records three in port_l2_out[]). Nothing may use
 * a range not listed here, and anything added must be added HERE first.
 *
 *   [0]        'WATC'
 *   [1..4]     the four watched 68K cell addresses
 *   [5..9]     floor_collide's table: f142, pointer, entry[0..2]
 *   [16]       'FLOR'   [17] watch-stack overflows
 *   [18]       the four cells after the frame, packed one byte each
 *   [19]       'CELL'
 *   [20..31]   per watched cell: innermost writer, was, now  (3 words x 4)
 *   [32..63]   per object slot: slot, 7 fields                (8 words x 4)
 *   [64]       floor_collide entry count   [65] spare
 *   [66..93]   floor_collide entry samples (14 words x 2)
 *   [128..131] per watched cell: how many spans saw a change
 *   [136..165] the full change sequence for the LAST watched cell (3 x 10)
 *   [166]      how many entries that sequence has
 *   [94]       floor_collide EXIT count   [167..174] exit samples (4 x 2)
 */
volatile uint port_watch_out[192];
/* MOVED UP 2026-10-11: the fallback used to sit ~1100 lines below, under the seeded
 * harness that was L2M_WATCH's only reader. The live-arm in the entry hook reads it too
 * and the hook comes first in this file, so without this the probe build fails with
 * 'L2M_WATCH undeclared' at the hook. */
#ifndef L2M_WATCH
#define L2M_WATCH 0u
#endif
static volatile byte *watch_cell;
/* CHANGE 1 (2026-10-11): THE WATCH CAN POINT AT TILE RAM, NOT ONLY WORK RAM.
 * watch_cell was computed as `w + (L2M_WATCH - 0xFFC000)`, an offset into the port's work
 * RAM, so a tile-RAM address (0x400000-0x40D000) resolved to a wild offset into w[] and the
 * watch silently measured some other byte. The port's tile RAM is the 13-page shadow at
 * 0x06019000 (w68.h's A68VID), so map it there. Work RAM is unchanged and still needs `w`,
 * which only the seeded path has. */
#define WATCH_IS_TILE(a)  ((unsigned)(a) >= 0x400000u && (unsigned)(a) < 0x40D000u)
#define WATCH_TILE_PTR(a) ((volatile byte *)(0x06019000u + ((unsigned)(a) - 0x400000u)))
static byte watch_live_armed;   /* CHANGE 2: armed once, outside the seeded harness */
static byte watch_stack[WATCH_DEPTH];
static unsigned int watch_sp;
static unsigned int watch_nhit;

void __cyg_profile_func_enter(void *this_fn, void *call_site);
void __cyg_profile_func_exit(void *this_fn, void *call_site);
/* THE SHARED CLOCK, defined further down beside port_fn_count. Declared here because
 * both instrumentation hooks tick it and they come first in this file. */
extern volatile unsigned int port_fn_seq;

__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *this_fn, void *call_site)
{
    (void)call_site;
    /* PUSH UNCONDITIONALLY OR NOT AT ALL. The first version pushed only when there
     * was room but popped on every exit, so one overflow desynchronised the stack
     * for the rest of the frame and every later comparison was against another
     * function's saved value -- a wrong ATTRIBUTION that still looks like a result.
     * Overflow now sets a flag and disables the watch instead of skewing it. */
    /* ONE CLOCK FOR BOTH INSTRUMENTS. port_fn_seq ticks on EVERY instrumented entry,
     * so CPORT_COUNT's first-entry stamps and the watchpoint's change stamp are on the
     * same scale and can be COMPARED. Without that, "floor_collide set this cell" and
     * "FUN_00004D9A read it" are two facts with no order between them -- and the whole
     * remaining player defect is an ordering question (FULL-PORT.md 64). One increment
     * on a global, in a build that is already a probe. */
    port_fn_seq++;
    /* CHANGE 2 (2026-10-11): ARM ON THE LIVE PATH. The watch was armed only around
     * l2m_frame_body_seeded, so it saw one seeded frame and never the running attract --
     * where the tilemap writes this card hunts actually happen. Arm once, at the first
     * instrumented entry, while the save stack is still empty so it stays balanced.
     * Tile addresses only: a work-RAM watch needs `w` and keeps the seeded path. */
    if (!watch_live_armed && WATCH_IS_TILE(L2M_WATCH)) {
        watch_live_armed = 1;
        watch_sp = 0; watch_nhit = 0;
        port_watch_out[0] = 0x57415443u;          /* 'WATC' */
        port_watch_out[1] = (uint)L2M_WATCH;
        watch_cell = WATCH_TILE_PTR(L2M_WATCH);
    }
    if (watch_cell) {
        if (watch_sp < WATCH_DEPTH) watch_stack[watch_sp++] = *watch_cell;
        else { port_watch_out[17]++; watch_cell = 0; }
    }
    /* ENTRY SAMPLER. A condition inside a function must be read with the values it
     * actually sees. Twice now a probe read the SEEDED state and concluded a branch
     * should have run: floor_collide is the 6th call in FUN_0000ee80's tail and
     * hitbox_b_expand (the 4th) rewrites +0x54/56/58/5A first, so pre-frame values
     * say nothing about the test. This fires on ENTRY to the chosen function, with
     * the live A6. */
    if (probe_fn && this_fn == probe_fn) probe_sample();
    if (!trace_arm) return;
    if (port_trace_n < TRACE_N)
        port_trace[port_trace_n++] = (unsigned int)this_fn;
}

/* SOFTWARE WATCHPOINT: WHICH FUNCTION WROTE THIS CELL (2026-09-30).
 *
 * The multi-frame gate (FULL-PORT.md 20) says the port writes ~40 cells a frame
 * that the arcade never touched, and names them: object fields +0x00, +0x12, +0x1B,
 * +0x2C on every active slot. A static grep cannot find the writer -- 327 functions
 * in the unit write +0x2C -- and the SH-2 has no hardware watchpoint here.
 *
 * -finstrument-functions already brackets every function in the port unit. Save the
 * watched byte on ENTRY and compare on EXIT: a function whose span changed the cell
 * either wrote it or called something that did, and since callees exit FIRST, the
 * FIRST exit to report a change is the INNERMOST writer. That is the answer.
 *
 * Needs PORTTRACE=1; without it gcc emits no hooks and the watch reads zero. */
__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *this_fn, void *call_site)
{
    (void)call_site;
    /* EXIT SAMPLE for the probed function: the entry sampler says every one of
     * floor_collide's four comparisons passes on slot 16, and the watchpoint says
     * it never wrote +0x2C. Both cannot be true. Reading the field at its EXIT
     * separates "the branch did not run" from "the watchpoint missed the write",
     * which is the difference between a port bug and an instrument bug. */
    if (probe_fn && this_fn == probe_fn) {
        volatile byte *o = (volatile byte *)_r_unaff_A6;
        uint n = port_watch_out[94];
        port_watch_out[94] = n + 1u;
        if (n < 2u && o) {
            port_watch_out[167 + n * 4 + 0] = (uint)o;
            port_watch_out[167 + n * 4 + 1] = (uint)o[0x2c];
            port_watch_out[167 + n * 4 + 2] = ((uint)o[0x1a] << 8) | o[0x1b];
            port_watch_out[167 + n * 4 + 3] = 0x46455854u;    /* 'FEXT' */
        }
    }
    if (watch_cell && watch_sp) {
        byte was = watch_stack[--watch_sp];
        byte now = *watch_cell;
        /* EVERY HIT, IN ORDER -- not just the first. Stopping at the innermost
         * writer was wrong and it hid the answer: 0xFFC82C is zeroed by
         * FUN_0000ee80 line 96, floor_collide ORs 0x10 back in (the entry sampler
         * proves all three of its conditions pass), and the cell still ENDS 0x00 --
         * so a LATER call writes it again. walk_player_slots and
         * sprite_build_depth_banded both write other slots' fields, which is how a
         * cross-slot writer hides behind a within-slot one.
         *
         * The sequence is the diagnosis: the first entry is the innermost writer,
         * the last is the final value, and the outer frames in between report the
         * same change because their span contains it. */
        if (was != now && watch_nhit < 10u) {
            port_watch_out[136 + watch_nhit * 3 + 0] = (uint)this_fn;
            port_watch_out[136 + watch_nhit * 3 + 1] = (uint)was;
            port_watch_out[136 + watch_nhit * 3 + 2] = (uint)now;
            /* AND THE TICK IT HAPPENED AT, on the same clock CPORT_COUNT stamps its
             * first entries with. [176..185] -- the gap between [167..174] (the
             * floor_collide entry sampler) and [192]. Without a shared clock, "this
             * function changed the cell" and "that function read it" are two facts
             * with no ORDER between them, and the remaining player defect is purely
             * an ordering question. */
            port_watch_out[176 + watch_nhit] = port_fn_seq;
            watch_nhit++;
            port_watch_out[166] = watch_nhit;
        }
    }
}

/* floor_collide's own view, sampled on entry. [64] is a call counter so a zero
 * reading cannot be read as "the condition failed" when the truth is "never
 * called". The first TWO calls are kept: the dispatch visits several slots. */
extern uint floor_collide(void);
extern char ROM_BASE[];
__attribute__((no_instrument_function))
static void probe_sample(void)
{
    volatile byte *o = (volatile byte *)_r_unaff_A6;
    const byte *rom = (const byte *)ROM_BASE;
    uint n = port_watch_out[64];
    uint f142, tp, e;
    volatile uint *q;
    port_watch_out[64] = n + 1u;
    if (n >= 2u || !o) return;
    q = port_watch_out + 66 + n * 14u;
    q[0]  = (uint)o;
    q[1]  = ((uint)o[0x1a] << 8) | o[0x1b];
    q[2]  = ((uint)o[0x5a] << 8) | o[0x5b];
    q[3]  = ((uint)o[0x68] << 8) | o[0x69];
    q[4]  = ((uint)o[0x54] << 8) | o[0x55];
    q[5]  = ((uint)o[0x56] << 8) | o[0x57];
    q[6]  = ((uint)o[0x10] << 8) | o[0x11];
    q[7]  = (uint)o[0x2c];
    f142  = (uint)((volatile byte *)WRAM_BASE)[0x3142];
    {   const byte *p2 = rom + 0xDEC4u + (f142 << 2);
        tp = ((uint)p2[0] << 24) | ((uint)p2[1] << 16) | ((uint)p2[2] << 8) | (uint)p2[3];
    }
    q[8] = f142; q[9] = tp;
    for (e = 0; e < 3u; e++) {
        const byte *p2 = rom + (tp & 0xFFFFFu) + e * 2u;
        q[10 + e] = ((uint)p2[0] << 8) | (uint)p2[1];
    }
    q[13] = 0x464E5452u;                       /* 'FNTR' */
}

volatile uint port_l2_canary_lo[8];
volatile uint port_l2_out[80];
volatile uint port_l2_canary_hi[8];
#define CANARY 0xC0DEFACEu
static uint unknown_handlers;      /* handler addresses not in the table */

/* THE DISPATCHER, supplied rather than decompiled. Ghidra's body calls
 * `(**(code **)(pcVar1 + 2))()`: a 68K address out of work RAM, with no argument.
 * Neither survives the port -- the address means nothing to the SH-2, and the 68K
 * passed the slot base in A6. Both are restored here. An address the table does not
 * know is COUNTED, never called: calling it would be a wild jump. */
/* only_slot: -1 = every active slot (the real dispatcher), otherwise just that
 * one. The whole 155-byte error lives in this function, and ~5 slots are active,
 * so running them one at a time names the handler that is wrong. */
static int dispatch_only = -1;
/* dispatch_skip: run every active slot EXCEPT this one, for leave-one-out
 * attribution. dispatch_only answers "what can this slot do alone", which is a
 * different and much weaker question -- alone, every slot scores about the same
 * because the other slots' work is missing either way. */
static int dispatch_skip = -1;

/* HANDLERS KNOWN TO HANG, skipped so the rest of the frame can still be measured.
 * A hang is total: the driver never reaches its compare, so the whole instrument
 * reads zero and NOTHING is measurable. Skipping names the exclusion instead, and
 * port_l2_out[67] counts how many calls were skipped so the number above can never
 * be read as if the frame were complete.
 *
 * 0x16BF0 began hanging the moment the real program image reached the cart
 * (sh_src/port_rom_data.s): before that its jump-table reads returned zero and it
 * fell out early, so this hang was HIDDEN BY A SECOND BUG. It is a real defect in
 * that function or its callees and it is the next thing to fix. */
/* EMPTIED 2026-09-29: 0x16BF0's hang was floor_collide walking a table whose
 * base is a 68K pointer loaded from ROM data and used untranslated (P68 now).
 * The list stays as machinery -- a hang is total and un-diagnosable without
 * it -- but nothing is excluded, so the frame below is COMPLETE. */
static const unsigned int HANG[] = { 0u };
#define NHANG 0
static int seed_regs = 0;
void port_l2_seed(unsigned int slot);

/* THE REGION BYTE. 0 = the reference set the decompile was made from; 1.. = a regional
 * variant whose differences the title's glue switches on (altbeast: 1 = altbeastj,
 * games/altbeast/glue.c). Runtime, so the title menu can set it; `make ... PORTREGION=N`
 * sets the power-on value. */
#ifndef PORT_REGION
#define PORT_REGION 0
#endif
byte port_region = PORT_REGION;

#include "glue.c"   /* the title's runtime glue: games/<title>/glue.c (framework 3e) */

/* PORTHOT: this function alone is 22,053 cyc/vint of the master (5.7%) and it lives in
 * cart .text today. Spelled out rather than included, as the note at :1587 explains for
 * the types. .portcode selects only `.text .text.*` from portfull objects, so `.ramtext`
 * is not matched by it and needs no new output section; mars_start.s already copies it. */
#if defined(HOT2_port_object_dispatch)
#define PORTHOT PORTHOT2_SEC
#elif defined(PORT_HOT)
#define PORTHOT __attribute__((section(".ramtext")))
#else
#define PORTHOT
#endif
PORTHOT uint port_object_dispatch(void)
{
    byte *slot = (byte *)WRAM_BASE;                    /* 68K 0xFFC000 */
    volatile byte *idx  = (volatile byte *)WRAM_BASE + 0x3109;   /* DAT_fffff109 */
    volatile byte *solo = (volatile byte *)WRAM_BASE + 0x3148;   /* DAT_fffff148 */
    uint i;

    *idx = 0;
#ifdef PORT_FFWATCH
    { byte _a = ((volatile byte *)WRAM_BASE)[1], _b = ((volatile byte *)WRAM_BASE)[0x1822];
      byte _pa = (byte)(port_watch_out[176] >> 8), _pb = (byte)port_watch_out[176];
      if (_a == 0xFFu && _pa != 0xFFu) { port_watch_out[166]++; port_watch_out[167] = ((uint)((volatile byte *)WRAM_BASE)[0x3020] << 8) | ((volatile byte *)WRAM_BASE)[0x3021]; }
      if (_b == 0xFFu && _pb != 0xFFu) { port_watch_out[174]++; port_watch_out[175] = ((uint)((volatile byte *)WRAM_BASE)[0x3020] << 8) | ((volatile byte *)WRAM_BASE)[0x3021]; }
      port_watch_out[176] = ((uint)_a << 8) | _b; }
#endif
    port_watch_hook(0x398eu);
    for (i = 0; i < SLOTS; i++, slot += STRIDE) {
        if ((signed char)slot[0] >= 0)                 /* active = byte 0 bit 7 */
            goto next;
        if (dispatch_only >= 0 && (uint)dispatch_only != i)
            goto next;
        if (dispatch_skip >= 0 && (uint)dispatch_skip == i)
            goto next;
        if (*solo != 0 && *solo != *idx) {
            _r_unaff_A6 = slot;
            hide_object_sprite();
            goto next;
        }
        {
            unsigned int a68 = ((unsigned int)slot[2] << 24) | ((unsigned int)slot[3] << 16)
                             | ((unsigned int)slot[4] << 8)  |  (unsigned int)slot[5];
            unsigned int lo = 0, hi = port_handlers_n;
            while (lo < hi) {                          /* table is sorted by a68 */
                unsigned int mid = (lo + hi) >> 1;
                if (port_handlers[mid].a68 < a68) lo = mid + 1; else hi = mid;
            }
            {   uint h;
                for (h = 0; h < NHANG; h++)
                    if (a68 == HANG[h]) { port_l2_out[67]++; goto next; }
            }
            if (!(lo < port_handlers_n && port_handlers[lo].a68 == a68)) {
                /* UNKNOWN HANDLER CENSUS (2026-10-04): the demo's enemy spawner 0xF1C0 is
                 * installed from a ROM table and was in no instrument's root list; a
                 * miss here is silent. Keep the first 4 distinct misses in
                 * port_watch_out[188..191] (PORTWATCH's block, idle in a play build). */
                uint k;
                for (k = 0; k < 4u; k++) {
                    if (port_watch_out[188 + k] == a68) break;
                    if (port_watch_out[188 + k] == 0u) { port_watch_out[188 + k] = a68; break; }
                }
            }
            if (lo < port_handlers_n && port_handlers[lo].a68 == a68) {
                /* NAME THE HANG. A handler that never returns freezes the rom with
                 * no other symptom, and 139 of them are candidates. Publishing the
                 * slot and the 68K address BEFORE the call means an ares dump of a
                 * hung rom says exactly which handler, in one run. */
                port_l2_out[9]  = i;
                port_l2_out[10] = a68;
                /* arm on the first entry to the handler named in port_trace_want,
                 * so the trace is that handler's own first TRACE_N calls */
                if (a68 == port_trace_want && port_trace_n == 0) trace_arm = 1;
                port_l2_out[11]++;                     /* handlers entered */
                _r_unaff_A6 = slot;                    /* A6 = this object */
                {   /* BUILD THE REGISTER FILE. A6 is always the slot base -- that
                     * is what the 68K's dispatcher held and the only one derivable
                     * without the oracle. Under PORTREGS the rest come from the
                     * arcade's own file at this call site, which is a DIAGNOSTIC
                     * and not a port (tools/cport_dispregs.py says why). */
                    uint q;
                    /* DO NOT ZERO D3 AND D6. They are THREADED handler to handler:
                     * each handler's exit value is the next one's entry value, three
                     * transitions for three in the frame-matched oracle, and the
                     * 68K's dispatcher computes neither. Zeroing them here is why
                     * the chain could never form, and why supplying the dispatcher's
                     * registers only ever fixed the first thing each value touched. */
                    for (q = 0; q < 15u; q++)
                        if (q != 3u && q != 6u) port_cur_regs[q] = 0;
                    port_cur_regs[3] = _port_reg[3];
                    port_cur_regs[6] = _port_reg[6];
#ifdef PORT_DISPREGS
                    if (port_dispregs_have[i])
                        for (q = 0; q < 15u; q++)
                            port_cur_regs[q] = port_dispreg_fix(q, port_dispregs[i][q]);
#endif
                    port_cur_regs[14] = (uint)slot;    /* A6, always the real one */
                }
                /* EXPERIMENT (FULL-PORT.md 14): supply the OTHER inherited
                 * registers too, from the arcade's own register file at this
                 * call. Only slot 0's handler reads any besides A6, so only
                 * slot 0 can move -- which is the point: it is +17 bytes in the
                 * per-slot ladder and it either improves or it does not. */
                if (seed_regs) port_l2_seed(i);
                port_handlers[lo].fn(port_cur_regs);
                port_watch_hook(0x10000u | (a68 & 0xFFFFu));
#ifdef PORT_FFWATCH
                /* MEASUREMENT (2026-10-04): TRANSITION watches, last one wins; slots 160-176
                 * of port_watch_out (the CPORT_ARGLOG ring, off in this build). A = slot 0
                 * byte 1 (0xFFC001) becoming 0xFF; B = slot 48 byte 0x22 becoming 0xFF. */
                { byte _a = ((volatile byte *)WRAM_BASE)[1], _b = ((volatile byte *)WRAM_BASE)[0x1822];
                  byte _pa = (byte)(port_watch_out[176] >> 8), _pb = (byte)port_watch_out[176];
                  if (_a == 0xFFu && _pa != 0xFFu) { port_watch_out[160]++; port_watch_out[161] = i; port_watch_out[162] = a68; port_watch_out[163] = ((uint)((volatile byte *)WRAM_BASE)[0x3020] << 8) | ((volatile byte *)WRAM_BASE)[0x3021]; }
                  if (_b == 0xFFu && _pb != 0xFFu) { port_watch_out[168]++; port_watch_out[169] = i; port_watch_out[170] = a68; port_watch_out[171] = ((uint)((volatile byte *)WRAM_BASE)[0x3020] << 8) | ((volatile byte *)WRAM_BASE)[0x3021]; }
                  port_watch_out[176] = ((uint)_a << 8) | _b; }
#endif
                /* and carry what the handler's chain left into the next slot */
                port_cur_regs[3] = _port_reg[3];
                port_cur_regs[6] = _port_reg[6];
                if (a68 == port_trace_want) trace_arm = 0;
                port_l2_out[12]++;                     /* handlers RETURNED */
                port_watch_hook(0x398au);   /* after the post-handler bookkeeping */
            } else {
                unknown_handlers++;
                if (unknown_handlers == 1)
                    port_l2_out[7] = a68;              /* the first one, for triage */
            }
        }
next:
        port_watch_hook(0x398bu);   /* loop end */
        *idx = (byte)(i + 1);
    }
    port_watch_hook(0x398fu);   /* dispatcher END */
    return 0;
}

/* THE 68K->SH-2 CALL. Every indirect call in the decompiled program reads a 68K
 * program address out of memory. cport_prep.py rewrites each one to come here.
 *
 * An address the table does not know is COUNTED, NOT CALLED. That is the whole
 * point: the first PORTFULL rom jumped to one and span in main_irq for 40% of the
 * machine's cycles with no diagnosis but a profile. A counted miss is a number in
 * port_l2_out[13] and a rom that keeps running. */
/* THE FORMS' ATTACK LOOKUPS (0x5062, 0x7F58, 0x8D66, 0xA3B0, 0xAF4A, 0xBBC6, 0xC9B4) leave a
 * transition handler in A0 and the caller jumps through it with D5-D7 live (the input
 * bits). cport_prep rewrites every `bsr ; bcc ; jmp (a0)` site to this: one call instead
 * of four, because .portcode sits 4 KB under the cart's end. */
SEC_port_call68_at uint port_call68_at(unsigned int site, unsigned int a68);   /* defined just below */
PORTCODE uint port_dec_jump(unsigned int h, unsigned int d5, unsigned int d6, unsigned int d7)
{
    port_cur_regs[5] = d5; port_cur_regs[6] = d6; port_cur_regs[7] = d7;
    return port_call68_at(9, h);
}
SEC_port_call68_at uint port_call68_at(unsigned int site, unsigned int a68)
{
    unsigned int lo = 0, hi = port_handlers_n;
    while (lo < hi) {
        unsigned int mid = (lo + hi) >> 1;
        if (port_handlers[mid].a68 < a68) lo = mid + 1; else hi = mid;
    }
    if (lo < port_handlers_n && port_handlers[lo].a68 == a68)
        return port_handlers[lo].fn(port_cur_regs);
    port_l2_out[13]++;                       /* indirect calls MISSED */
    {   /* misses per SITE, in slots 56..63. NOT 16..23: [21] is the tick count,
         * [22] the canary verdict and [23] the VARM marker -- the third slot
         * collision in this array, caught before it produced a plausible wrong
         * number this time. Nothing else may use 56..71. */
        static uint per_site[8];
        if (site < 8) { per_site[site]++; port_l2_out[56 + site] = per_site[site]; }
    }
    if (port_l2_out[14] == 0)
        port_l2_out[14] = a68;               /* the first one, for triage */
    {   /* the first 8 DISTINCT missed targets: one repeated miss and eight
         * different ones need different fixes, and the count alone cannot tell
         * them apart. 0 is a legitimate miss value (a table read as zeros), so a
         * separate occupancy slot is needed rather than testing for 0. */
        uint k;
        for (k = 0; k < 8; k++) {
            if (port_l2_out[48 + k] == 0) {          /* free slot */
                port_l2_out[48 + k] = 1u;            /* occupied */
                port_l2_out[40 + k] = a68;
                break;
            }
            if (port_l2_out[40 + k] == a68)
                break;                               /* already recorded */
        }
    }
    return 0;
}

#define FRT16() ((uint)(unsigned short)((SH2_FRT_FRCH << 8) | SH2_FRT_FRCL))


/* WHICH CALL SEQUENCE IS THE FRAME? The capture boundary is MAME's frame_done,
 * which is consistent but says nothing about where the game's own loop stood --
 * and the loop body is FUN_00003aae, FUN_0000144a, object_dispatcher with IRQ4
 * landing asynchronously. Rather than assume, run every candidate against the
 * same seed and let the byte count choose. Variant 0 is the CONTROL: it runs
 * nothing, so it scores exactly the frame's own delta (40), and any variant that
 * does not beat 40 is doing net harm. */
/* MMIO polarity was TESTED AND REFUTED (2026-09-28): S16B inputs are active low,
 * so a zeroed stub should read as every button held -- but filling the stub with
 * 0xFF instead of 0x00 changed not one byte of the score. This path does not read
 * those stubs. The fill stays, at 0xFF, because "nothing pressed" is the honest
 * resting state; it is no longer a variable. */
extern char MMIO_BASE[];
void port_reset_pseudoregs(void);
/* THE WHOLE STUB REGION, from the linker, not a guess. cport_layout.py compacts the
 * 68K's 8.6 MB of MMIO space into one 4 KB page per distinct 68K page and emits the
 * total as _MMIO_BYTES. It used to be hardcoded at 0x1000 while the symbols were laid
 * out LINEARLY over 8.6 MB, so most of them fell outside SDRAM and reached a cell only
 * because the 32X mirrors SDRAM every 256 KB -- which also collided DAT_00440000 with
 * DAT_00840000, and left DAT_00c41003 and DAT_00c41007, THE TWO CONTROLLER PORTS,
 * outside the fill entirely (FULL-PORT.md 39). */
extern char MMIO_BYTES_SYM[];
#define MMIO_BYTES ((uint)(unsigned long)MMIO_BYTES_SYM)

/* THE FOUR S16 VIDEO REGIONS NOW SIT ON THE RENDERER'S OWN SDRAM SHADOWS
 * (tools/cport_layout.py): the port's picture IS the renderer's input, so there is
 * nothing to transport and the output path is a layout decision rather than a
 * mechanism.  They are therefore NO LONGER contiguous with MMIO_BASE, which from
 * here on names only the sparse I/O block, and the reset has to walk them one by
 * one.  Bases and sizes come from the linker so this file cannot disagree with the
 * layout -- a hardcoded 0x1000 here is exactly how DAT_00c41003 and DAT_00c41007,
 * THE TWO CONTROLLER PORTS, fell outside the fill for a week (FULL-PORT.md 39). */
extern char VID0_BASE[], VID1_BASE[], VID2_BASE[], VID3_BASE[];
extern char VID0_BYTES_SYM[], VID1_BYTES_SYM[], VID2_BYTES_SYM[], VID3_BYTES_SYM[];
#define VIDN 4
static volatile byte *const port_vid_base[VIDN] = {
    (volatile byte *)VID0_BASE, (volatile byte *)VID1_BASE,
    (volatile byte *)VID2_BASE, (volatile byte *)VID3_BASE,
};
static const uint port_vid_len[VIDN] = {
    (uint)(unsigned long)VID0_BYTES_SYM, (uint)(unsigned long)VID1_BYTES_SYM,
    (uint)(unsigned long)VID2_BYTES_SYM, (uint)(unsigned long)VID3_BYTES_SYM,
};
/* 0xFF over the I/O block, 0xFF over the video regions TOO -- deliberately the same
 * byte the single contiguous block used, so this layout change moves the base
 * addresses and NOTHING ELSE about the measurement.  Resting video memory is really
 * zeros (md_main.c zeroes the palette mirror at boot because boot RAM is random),
 * and that is a separate experiment with its own score, not a free rider here. */
PORTCODE static void port_mmio_reset(void)
{
    volatile byte *io = (volatile byte *)MMIO_BASE;
    uint k, r;
    for (k = 0; k < MMIO_BYTES; k++) io[k] = 0xFFu;
    for (r = 0; r < VIDN; r++) {
        volatile byte *v = port_vid_base[r];
        uint n = port_vid_len[r];
        for (k = 0; k < n; k++) v[k] = 0xFFu;
    }
}

/* THE SAME REGIONS, ZEROED. 0xFF is a POISON value: the gate fills with it so a cell
 * the port never wrote is distinguishable from one it wrote as zero, and that is
 * exactly right for a measurement. It is wrong for a PICTURE -- 0xFF is a legal tile
 * index and a legal colour, so a poisoned tilemap draws tile 0xFFFF everywhere. A
 * play build starts from a blank screen, which is what the arcade's own boot leaves
 * before clear_tileram runs. */
PORTCODE static void port_mmio_zero(void)
{
    volatile byte *io = (volatile byte *)MMIO_BASE;
    uint k, r;
    for (k = 0; k < MMIO_BYTES; k++) io[k] = 0x00u;
    for (r = 0; r < VIDN; r++) {
        volatile byte *v = port_vid_base[r];
        uint n = port_vid_len[r];
        for (k = 0; k < n; k++) v[k] = 0x00u;
    }
}

/* A CUMULATIVE LADDER. The capture point is known: tools/l2_where.lua says the 68K
 * is spinning at PC 0x003984-0x003988 -- the frame-flag barrier -- at EVERY
 * frame_done in the window, so frame N to N+1 is exactly one iteration of the loop
 * body. Adding one call at a time therefore attributes every wrong byte to the
 * call that introduced it. */
#define NVARIANT 16
PORTCODE static void run_variant(uint v)
{
    /* The `v >= n` ladder is for variants 0..4 ONLY. Variants 5..7 repeat it
     * without irq4, and an earlier version wrote them as further `v ==` tests
     * inside the same chain -- so they ran the whole ladder AND their own extra,
     * and reported numbers for a sequence nobody asked for. Split explicitly. */
    if (v <= 4) {
        if (v == 0) return;                                      /* control */
        if (v >= 1) { port_l2_out[1] = 1; irq4_handler(); }
        if (v >= 2) { port_l2_out[1] = 2; FUN_00003aae(); }
        if (v >= 3) { port_l2_out[1] = 3; FUN_0000144a(); }
        if (v >= 4) { port_l2_out[1] = 4; port_object_dispatch(); }
        return;
    }
    if (v <= 7) {
        /* 5..7: the same ladder WITHOUT irq4, to separate its contribution */
        port_l2_out[1] = 2;  FUN_00003aae();
        if (v >= 6) { port_l2_out[1] = 3; FUN_0000144a(); }
        if (v >= 7) { port_l2_out[1] = 4; port_object_dispatch(); }
        return;
    }
    if (v >= 14) {
        /* 14: the full frame WITH the oracle's registers supplied at each
         * dispatch. 15: slot 0 alone, with them. Compare against 4 and 8. */
        seed_regs = 1;
        port_l2_out[1] = 1;  irq4_handler();
        port_l2_out[1] = 2;  FUN_00003aae();
        port_l2_out[1] = 3;  FUN_0000144a();
        port_l2_out[1] = 4;
        dispatch_only = (v == 15) ? 0 : -1;
        port_object_dispatch();
        dispatch_only = -1;
        seed_regs = 0;
        return;
    }
    /* 8..13: the full frame, but dispatching ONE object slot. The capture has four
     * active slots (0, 16, 17, 40); the extra rows cost nothing and catch a fifth
     * if a different capture has one. */
    {
        static const signed char SLOT[6] = { 0, 16, 17, 40, 41, 63 };
        port_l2_out[1] = 1;  irq4_handler();
        port_l2_out[1] = 2;  FUN_00003aae();
        port_l2_out[1] = 3;  FUN_0000144a();
        port_l2_out[1] = 4;
        dispatch_only = SLOT[v - 8];
        port_object_dispatch();
        dispatch_only = -1;
    }
}

/* ===================== THE MULTI-FRAME GATE (2026-09-30) =====================
 *
 * ONE FRAME IS NOT PLAY. Every number in HANDOFF 5 is a single frame: seed 9000,
 * run the body, diff against 9001. That cannot distinguish a port that reproduces
 * the game from one that happens to be right once, and Mike's position is that a
 * play candidate REQUIRES this port running -- so the question the gate has to
 * answer is whether it SUSTAINS, not whether it is nearly right once.
 *
 * TWO CURVES, and they answer different questions:
 *
 *   FREE-RUN     seed once, run N frames, never re-seed. Compare frame k against
 *                the arcade's frame 9000+k. Error COMPOUNDS -- this is "is it
 *                running the game".
 *   PER-FRAME    re-seed from the arcade's frame 9000+k-1 each time, run one
 *                frame, compare against 9000+k. Each frame is independent -- this
 *                is "is 49 typical or was 9000 a lucky frame".
 *
 * NO 16 KB EXPECTED BUFFER. There is ~3 KB of headroom under the region guard at
 * 0x06019000 (FULL-PORT.md 7), so expected(k) is reconstructed on the fly: it is
 * the seed with frames 1..k's delta records applied, each list is SORTED by
 * offset, and a k-way merge over the RECORDS (<= 48 per frame) walks in lockstep
 * with one ascending pass over work RAM. Cost is O(16384 + records*k) per frame,
 * not O(16384*k), and the RAM cost is one cursor per frame -- 128 bytes.
 */
extern const byte l2_frames[];          /* sh_src/port_l2_frames.s */



/* HOW MANY FRAMES THE CHAIN RUNS, AND WHY IT IS NOT 30.
 *
 * It was 30, and 30 was never derived -- it is this repo's house unit for a
 * sampling WINDOW (MESSAGES.md:852, FACTS.md:655) and, worse, 30 is the CADENCE
 * BARRIER this whole port exists to break (FULL-PORT.md 1: "the cuts path cannot
 * reach 60Hz"). Borrowing it as a sample size was a collision with the project's
 * most loaded number. See LESSONS, "A HOUSE UNIT IS NOT A JUSTIFIED SAMPLE SIZE".
 *
 * The dense curves now run as many frames as TWO measured ceilings allow:
 *   CART     .portdata plus the cart's 73 KB of spare holds 574 frames of delta
 *            records at 159 B/frame (measured 2026-09-30).
 *   SCORER   l2m_score's merge is O(distinct offsets x k), so the per-frame curve
 *            is O(k^2) overall: 512 frames costs ~14 s of EMULATED time per curve
 *            and would not finish before the dump frame. 256 costs ~4 s.
 * The scorer binds first, so the dense length is 256 frames = 4.3 s at 59.92 Hz.
 *
 * Endurance is a separate, longer run scored HOST-side at its endpoint
 * (tools/portl2_endure.py), which costs no cart space at all and uses the repo's
 * own probe unit of 900 frames. */
#define L2M_MAX 288u
volatile uint port_l2m_canary_lo[4];
/* 160 words. [8..37] free-run per frame, [40..69] per-frame per frame, [0..7] and
 * [70..79] summary, [96..127] the spurious cells, [128..159] the missed cells.
 * Nothing may reuse a range -- three slot collisions in port_l2_out[] cost a day
 * between them (HANDOFF 5 trap 8). */
volatile uint port_l2m_out[256];
volatile uint port_l2m_canary_hi[4];

/* ZOOM. The per-frame curve is downsampled to 32 points, which located two STEPS
 * -- f113 reads 60 where the control is 34, f201 reads 98 against 52 -- but not the
 * frame they start on. Built with PORTZOOM=N the per-frame block becomes frames
 * N..N+31 at FULL resolution, and the collector fires on the first frame in that
 * window whose score jumps by >= L2M_STEP over its predecessor, so the window finds
 * its own step rather than being told where it is. Per-frame re-seeds from the
 * arcade every frame, so a step here is a defect the port owns outright -- there is
 * no inherited error to blame. */
#ifndef L2M_ZOOM
#define L2M_ZOOM 0u
#endif
#ifndef L2M_ENDURE
#define L2M_ENDURE 0u
#endif
/* PORTSEED=N + PORTSKIP=S: the PER-FRAME gate with a HOST-side cell list.
 *
 * Seed work RAM to the arcade's frame N (apply deltas 1..N), run ONE frame body,
 * and leave it. The host diffs against the capture at N+1. No inherited error, and
 * unlike the in-rom collector the host sees EVERY wrong cell.
 *
 * PORTSKIP=S dispatches every active slot except S. Running the same seed with and
 * without a slot and diffing the two host cell lists gives that slot's exact
 * contribution -- the leave-one-out ablation said skipping slot 40 is worth 10 cells
 * a frame, and this names which ten. */
#ifndef L2M_SLOTMASK_AT
#define L2M_SLOTMASK_AT 8u
#endif
#ifndef L2M_SEEDAT
#define L2M_SEEDAT 0u
#endif
#ifndef L2M_SKIP
#define L2M_SKIP (-1)
#endif
/* PORTWATCH=0xADDR: watch ONE 68K cell through a PORTSEED frame and publish the
 * sequence of functions whose span changed it. The curves path watches a fixed
 * list; this makes the cell a build parameter so a named defect can be chased
 * without editing the driver. Needs PORTTRACE=1 for the hooks. */
#define L2M_STEP 8u
static int  l2m_collect;              /* record the offsets, not just the counts */
static uint l2m_nmiss, l2m_nspur;
/* U16 COUNTS AND CURSORS, not pointer arrays. Two arrays of 288 pointers is
 * 2,304 bytes of .bss and there are ~1.8 KB of headroom under the region guard at
 * 0x06019000; the start of frame k's records is derivable from the blob header, so
 * only the record COUNT and the walk CURSOR need storing. */
/* BYTES, not shorts: 288 x 2 arrays of shorts is 1,152 B and the region guard at
 * 0x06019000 refused the build (".bss end 0x06019160 crosses SDRAM region base").
 * The packer measures 15..140 records per frame, so a byte holds both the count and
 * the cursor -- and l2m_index() REFUSES to run the chain if that ever stops being
 * true, rather than truncating a frame's record list and scoring the result. */
static byte fcnt[L2M_MAX];
static byte fcur[L2M_MAX];
static uint l2m_n;

#define FSTART(k) ((const unsigned short *)(l2_frames + ((const uint *)l2_frames)[k] + 4))

PORTCODE static void l2m_index(void)
{
    const uint *h = (const uint *)l2_frames;
    uint k;
    l2m_n = h[0];
    if (l2m_n > L2M_MAX - 1u) l2m_n = L2M_MAX - 1u;
    for (k = 1; k <= l2m_n; k++) {
        uint cnt = *(const uint *)(l2_frames + h[k]);
        if (cnt > 255u) {                  /* a byte cursor cannot walk it */
            port_l2m_out[166] = cnt;       /* publish WHY, then run nothing */
            l2m_n = 0;
            return;
        }
        fcnt[k] = (byte)cnt;
    }
}

/* apply ONE frame's delta to work RAM: the arcade's own writes for that frame */
PORTCODE static void l2m_apply(volatile byte *w, uint k)
{
    const unsigned short *p = FSTART(k), *e = p + 2u * fcnt[k];
    while (p < e) { w[p[0]] = (byte)p[1]; p += 2; }
}

/* THE SCORE against expected(k). `cur` is the merge state: the lowest head among
 * the k lists is the next offset the arcade changed, and when two lists hold the
 * same offset the LATER FRAME's value wins because it was written later. */
/* THE SPLIT THAT NAMES THE WORK. `wrong` alone cannot tell two opposite failures
 * apart: not DOING the arcade's writes, and doing writes the arcade did not. For a
 * cell the arcade's frame CHANGED, a mismatch is MISSED WORK; for a cell whose end
 * value it did NOT change, a mismatch means we left it somewhere else.
 * miss + spur == wrong, always.
 *
 * `spur` IS NOT "the arcade never touched this cell" -- that reading was wrong and
 * l1_vectors.jsonl refuted it the same day: across 669 captured calls the arcade
 * writes 0xFFC800 46 times and 0xFFC82C 10 times and still ENDS the frame where it
 * started. The honest name is UNCHANGED-BUT-WRONG (FULL-PORT.md 21). */
/* K IS CLAMPED, AND THAT IS NOT COSMETIC. l2m_score walks fcnt[]/fcur[] for
 * j = 1..k. Called with k = 900 (PORTENDURE=900) against a 256-frame delta chain
 * and L2M_MAX = 288, it READ out of bounds and, worse, WROTE `fcur[j] = 0` for
 * j up to 900 -- 612 bytes past the end of a .bss array. Measured 2026-10-01: the
 * run reported "212 wrong, missed 0" against 1,168 genuinely wrong cells, and
 * `missed 0` with a four-figure wrong count is the only reason it was caught.
 *
 * Beyond the chain there is simply no expected image to score against, so the
 * score is REFUSED (0xFFFFFFFF) rather than computed from clamped data that would
 * look like a result. The host-side endpoint diff is the valid instrument there. */
/* FREE-RUN PER-SLOT TRACKING. Off by default so the per-frame curve and the
 * ablation passes -- which re-seed and re-score dozens of times -- cannot write into
 * it. The free-run loop is the only caller that turns it on, because it is the only
 * one where "the frame a slot first went wrong" means anything. */
static byte l2m_slotfirst[SLOTS];     /* frame 1..255, 0 = never wrong */
static byte l2m_slotwrong[SLOTS];     /* wrong bytes this scored frame */
static uint l2m_slotframe;            /* the frame being scored */
static int  l2m_slotmark;             /* 1 = record into the two arrays above */
static uint l2m_offmask[4];           /* byte-offset-within-slot mask, this frame */

PORTCODE static uint l2m_score(volatile byte *w, uint k, uint *firstp, uint *reg,
                               uint *missp, uint *spurp)
{
    uint j, i, wrong = 0, first = 0xFFFFFFFFu;
    uint nxt_off = 0x10000u, nxt_val = 0;
    uint miss = 0, spur = 0;

    for (j = 0; j < 4; j++) reg[j] = 0;
    if (l2m_slotmark) {
        for (j = 0; j < SLOTS; j++) l2m_slotwrong[j] = 0;
        for (j = 0; j < 4; j++) l2m_offmask[j] = 0;
    }
    *missp = *spurp = 0; *firstp = 0xFFFFFFFFu;
    if (k > l2m_n || k >= L2M_MAX)
        return 0xFFFFFFFFu;          /* no expected image exists for this k */
    for (j = 1; j <= k; j++) fcur[j] = 0;
    for (;;) {                             /* prime the merge */
        uint best = 0x10000u, val = 0;
        for (j = 1; j <= k; j++)
            if (fcur[j] < fcnt[j] && FSTART(j)[fcur[j]*2] <= best)
                { best = FSTART(j)[fcur[j]*2]; val = FSTART(j)[fcur[j]*2+1]; }
        if (best == 0x10000u) break;
        for (j = 1; j <= k; j++)
            if (fcur[j] < fcnt[j] && FSTART(j)[fcur[j]*2] == best) fcur[j]++;
        nxt_off = best; nxt_val = val;
        break;
    }
    for (i = 0; i < WRAM_BYTES; i++) {
        byte exp;
        int changed = 0;
        if (i == nxt_off) {
            exp = (byte)nxt_val;
            changed = 1;
            {   /* advance the merge to the next distinct offset */
                uint best = 0x10000u, val = 0;
                for (j = 1; j <= k; j++)
                    if (fcur[j] < fcnt[j] && FSTART(j)[fcur[j]*2] <= best)
                        { best = FSTART(j)[fcur[j]*2]; val = FSTART(j)[fcur[j]*2+1]; }
                for (j = 1; j <= k; j++)
                    if (fcur[j] < fcnt[j] && FSTART(j)[fcur[j]*2] == best) fcur[j]++;
                nxt_off = best; nxt_val = val;
            }
        } else {
            exp = l2_before[i];
        }
        if (w[i] != exp) {
            wrong++;
            if (changed) miss++; else spur++;
            if (l2m_slotmark && i < 0x2000u) {
                uint sl = i >> 7;                  /* STRIDE = 0x80 */
                uint off = i & 0x7Fu;
                l2m_offmask[off >> 5] |= 1u << (31u - (off & 31u));
                if (l2m_slotwrong[sl] < 255u) l2m_slotwrong[sl]++;
                if (!l2m_slotfirst[sl])
                    l2m_slotfirst[sl] = (byte)(l2m_slotframe > 255u ? 255u
                                                                    : l2m_slotframe);
            }
            /* NAME THE CELLS, not just the count. 40 spurious writes a frame is a
             * defect in one or two places, and the OFFSET says which: offset/0x80
             * is the object slot and offset%0x80 the field. Collected on the last
             * frame of the per-frame curve only -- every frame's list is nearly
             * the same and one is enough to look up. */
            if (l2m_collect) {
                volatile uint *dst = port_l2m_out + (changed ? 132 : 100);
                uint c = changed ? l2m_nmiss : l2m_nspur;
                if (c < 32u) {
                    dst[c] = 0xFFC000u + i;
                    if (changed) l2m_nmiss = c + 1; else l2m_nspur = c + 1;
                }
            }
            if (first == 0xFFFFFFFFu) first = i;
            /* WHICH REGION, because one of them may not be winnable: the 31-frame
             * run differs from the 2026-09-27 pair by 30 bytes and ALL of them are
             * at 0xFFFF00+, so that region is not reproducible even between two
             * MAME runs of the same playback. */
            if      (i < 0x2000u) reg[0]++;        /* objects 0xFFC000-0xFFDFFF */
            else if (i < 0x3000u) reg[1]++;        /* 0xFFE000 */
            else if (i < 0x3F00u) reg[2]++;        /* globals 0xFFF000-0xFFFEFF */
            else                  reg[3]++;        /* 0xFFFF00+ */
        }
    }
    *firstp = first;
    *missp = miss; *spurp = spur;
    return wrong;
}

/* THE FRAME BODY. irq4_handler here is the GAME's own 68K VBL handler at 0x02AAC,
 * part of the decompiled program -- not a transport signal.
 *
 * THE IRQ4-ORDERING AXIS IS CLOSED. Running it last instead of first scored
 * identically to the byte over 30 frames (branch counters proved both ran), and
 * Mike closed it outright: "that was the old model, using IRQ4 as the signal to
 * carry vint when using the framebuffer as a message queue. We have no use for
 * that" -- with the logic on the SH-2 the port is not hamstrung by the 68K and
 * owns its own output path. The flag and its two counters are gone. */
/* THE FRAME BODY -- REVERTED to the four direct calls on 2026-10-01 after the
 * fifteen-call version measured WORSE at every length (tools/port_loop.py):
 *
 *     1 call  46 wrong net  -6      5 calls 61 net  +9
 *     2-4     54 wrong net  +2      6-7     69 net +17
 *     all 14  97 wrong net +45  (hangs in 0x0E052, entered and never returned)
 *
 * against -43 for these four. The sequence was right and the IMPLEMENTATION was
 * wrong, and the 68K says why (docs/audit/frame_loop.asm, 0x970-0xADC):
 *
 *   * IT HAS BRANCHES. `btst.b #0,(0xfffff026).w ; bne 0xABE` conditionally skips
 *     the 0x0DFA call, and `cmpi.b #6,(0xfffff142).w` gates more. A flat
 *     unconditional list runs calls the arcade skips -- which is exactly the
 *     monotonic worsening the loop measured.
 *   * TWO CALLS WERE MISSING from the oracle's 15: 0x09052 at 0x996 and 0x17188 at
 *     0x0ACE. The L1 window did not capture them.
 *   * A6 IS NEVER SET in that range, so dispatching through port_call68_at -- which
 *     passes port_cur_regs, whose [14] is the LAST DISPATCHED SLOT -- handed ten of
 *     the fourteen an A6 pointing at an object record.
 *
 * THE REAL FIX IS NOT A LIST. The whole loop lives inside one decompiled function
 * (0x005BE-0x00BD5, which Ghidra named `test_mode_screen` -- the main routine,
 * mis-named), and it is NOT in the port's unit. Adding it as a root and calling it
 * once per frame lets the decompiled code do its own branching instead of me
 * reimplementing it. That is the next step, and it is an architecture change rather
 * than a flag. */

/* ---- PORTMAIN: THE REAL MAIN LOOP DRIVES, AND THIS DRIVER IS ITS VSYNC --------
 *
 * 0x005BE is the game's entry (`lea 0xffffff00,%sp ; movew #8960,%sr` then an
 * infinite do/while whose top is 0x62A). Ghidra named it `test_mode_screen`; it is
 * the main routine, mis-named. Rooting it and letting it branch for itself is the
 * whole point of this step -- the alternative, a flat list of the fifteen calls the
 * L1 window saw, measured monotonically worse at every length because the arcade's
 * loop SKIPS calls on `btst.b #0,(0xfffff026).w` and `cmpi.b #6,(0xfffff142).w`.
 *
 * A FUNCTION THAT NEVER RETURNS STILL HAS A FRAME BOUNDARY: `jsr 0x397e`
 * (wait_frame_flag) at 0x640, four instructions into the loop. CPORT_STUBS
 * redirects it here. So:
 *
 *   yield 1   the PROLOGUE has just finished (0x5BE..0x63E). SEED HERE -- the
 *             prologue calls clear_textram/clear_tileram/clear_spriteram_and_pool
 *             and would erase a seed placed before the call. Seeding at the first
 *             vsync is also exactly where the arcade's capture was taken.
 *   yield 2   one full iteration of the loop has run. Work RAM is the result.
 *             Leave through __builtin_longjmp -- the driver's frame is still live
 *             below us, which is the one legal use of a non-local exit.
 *
 * WHY NOT A COROUTINE. A longjmp cannot RESUME, so every frame re-enters at 0x5BE
 * and re-runs the prologue. For the SEEDED frame and the PER-FRAME curve that is
 * harmless: the prologue only writes work RAM and the seed overwrites all 16 KB of
 * it afterwards. FREE-RUN IS DIFFERENT -- it must carry state across the boundary,
 * and a re-run prologue erases it, which would silently turn the free-run curve
 * into a second copy of the per-frame curve. So PORTMAIN REFUSES free-run (below)
 * rather than report a number that means something else. A real resume needs a
 * second stack and a context switch; that is its own step.
 */
#ifdef PORT_MAINLOOP
/* Ghidra's signature is five register parameters, all undefined4 (= unsigned int,
 * sh_src/portfull/ghidra_types.h:6). Spelled out rather than included because this
 * driver does not pull in the generated headers. */
extern void frame_rotated(void);   /* 0x0AE2 -- ONE rotated arcade frame */
extern void port_reset_sp(void);   /* generated: 68K A7 <- 0xFFFFFF00 */

#if defined(PORT_BOOT) && defined(PORT_PLAY)
extern void FUN_0000040e(void);   /* the 68K reset vector's target, 0x40E.
                                  * File scope, NOT inside the PORT_CORO block:
                                  * the non-coroutine boot path needs it too. */
#endif
extern volatile uint port_play_dbg[8];   /* defined with the play tick below */
static void *mainloop_exit[5];
static int   mainloop_armed;      /* 0 = the hook is inert (the four-call body) */

#ifdef PORT_CORO
/* ---- THE COROUTINE: `frame_rotated` YIELDS AND RESUMES --------------------------
 *
 * WHAT RE-ENTRY COSTS. Free-run already works at the 0x0AE2 entry: there is no
 * prologue to erase, so re-entering runs tail, head, body, vsync -- exactly one more
 * frame -- and the game's state lives in work RAM, which carries across. What does
 * NOT carry across is the SH-2 CALL STACK. At every frame boundary the port drops
 * however many frames of decompiled 68K code were live and starts from the root; the
 * real 68K drops nothing, because its `jsr 0x397E` simply returns.
 *
 * That matters because of WHERE the 68K's own stack lives. It is emulated in work
 * RAM through register0x0000003c (A7), and the driver reloads it to 0xFFFFFF00 every
 * frame because the arcade's loop does (0x62A, an instruction Ghidra dropped). So
 * 68K-visible stack state is faithful. But any value the decompiler chose to keep in
 * an SH-2 LOCAL rather than through A7 -- and at -Os that is most of them -- lives on
 * the SH-2 stack and is gone.
 *
 * THE HYPOTHESIS, and it has an instrument already pointed at it: the free-run curve
 * diverges (+31.7 B/frame against the control over 176 frames) while the per-frame
 * curve is solidly correct (-25.6). A per-frame error that only compounds is exactly
 * the signature of state lost at the boundary. If the lost SH-2 stack is that state,
 * the coroutine moves free-run toward per-frame; if it is not, free-run does not
 * move and the suspect is eliminated for the cost of one run. Either answer is worth
 * having -- PORT-OUTPUT.md 6 makes the free-run gap the only thing between the port
 * and a playable build.
 *
 * Switch and launch are in sh_src/mars_start.s; the stack is .portstack (mars.ld). */
extern void  port_ctx_switch(void **save_sp, void *load_sp);
extern void *port_ctx_make(void *stack_top, void (*entry)(void));
extern char  PORT_CORO_BASE[], PORT_CORO_TOP[];

#ifdef PORT_KINDCENSUS
/* ONE FLAT LIST, 128 BYTES: a per-round table overflowed .bss into the renderer's
 * region at 0x06019000. Each entry is (round << 24) | handler; the handler is 17 bits
 * so the top byte is free. */
#define PORT_KIND_MAX 32
volatile uint port_kind_seen[PORT_KIND_MAX];
volatile uint port_kind_n;
#endif
static void *coro_drv_sp;     /* the driver's SP while the game runs */
static void *coro_game_sp;    /* the game's SP while the driver runs */
static uint  coro_live;       /* the game coroutine exists and is suspended */
static uint  coro_returned;   /* frame_rotated RETURNED -- it is a loop, so never */

#define CORO_PAINT 0xA5A5A5A5u


/* ---- THE VBLANK IRQ'S FAKE-RETURN REDIRECTS -------------------------------------
 *
 * The 68K's VBL handler does not always return to the interrupted code. At four
 * points it builds a FAKE EXCEPTION FRAME and rte's somewhere else, abandoning the
 * game thread entirely -- the idiom is `pea target.l ; move.w #$2300,-(sp) ; rte`.
 * Traced on the arcade in MAME (advisor, 2026-10-18), handler tail 0x2C1A-0x2DBC:
 *     0x2C98  pea 0x1E4E   a credit landed while a demo runs   -> the title
 *     0x2D76  pea 0x05BE   START pressed while a demo runs     -> main_entry, the GAME
 *     0x2D9C  pea 0x1AFDE  service / test
 *     0x2DB0  pea 0x0400   reset
 * THE DECOMPILER DROPS ALL FOUR. Each became a plain `return`, so the state writes
 * that precede them survive and the redirect does not -- which is exactly why a coin
 * never cut the attract and START only kept the demo playing: f026/f028/f000 were
 * updated and then the game thread resumed the demo it was already in.
 *
 * WHY THIS IS A FLAG AND NOT AN UNWIND: irq4_handler runs on the DRIVER's stack
 * (port_play_tick calls it immediately before the `if (!coro_live)` below), not on
 * the game coroutine. So abandoning the interrupted thread is just clearing
 * coro_live: the driver then repaints the stack and makes a fresh coroutine, and
 * coro_entry starts it at the target. That IS the 68K's semantics, not an
 * approximation of them.
 *
 * ONLY THE TWO TARGETS THAT MATTER ARE WIRED. 0x1AFDE (service) and 0x0400 (reset)
 * are left exactly as they are today -- the pass does not mark them -- because a
 * wrong guess there hangs the attract and neither is on any gate. Recorded as
 * known-unhandled rather than approximated. */
extern uint main_entry(void);
/* `undefined4` is unsigned int (ghidra_types.h:6); spelled out because this
 * driver does not include the generated headers, as the notes above explain. */
extern uint FUN_00001e54(uint, uint, uint, uint, uint);
extern byte DAT_fffff031;
static uint irq_redirect;              /* pending 68K target, 0 = none */
uint port_irq_redirects;               /* how many fired, for the reader */

PORTCODE void port_irq_redirect(uint target)
{
    irq_redirect = target;
    port_irq_redirects++;
    coro_live = 0;                     /* discard the interrupted game thread */
}

/* The coroutine's body. `frame_rotated` is a do{}while that never exits, so this
 * never returns either; if it ever does, say so in a counter the reader publishes
 * rather than switching back out of a dead stack. */
static void coro_entry(void)
{
    {   /* A REDIRECT STARTS THE FRESH THREAD WHERE THE rte WOULD HAVE LANDED. */
        uint _t = irq_redirect;
        irq_redirect = 0;
        if (_t == 0x1E4Eu) {
            /* 0x1E4E is six bytes above the sequencer and its whole content is
             * `move.b #4,0xFFF031`; falling into 0x1E54 then adds 4, so the step
             * lands on 8, the title. FUN_00001e54 begins AT 0x1E54, so the step
             * write has to be done here or the step just advances normally. */
            DAT_fffff031 = 4;
            FUN_00001e54(0, 0, 0, 0, 0);
            coro_returned++;
            for (;;) port_ctx_switch(&coro_game_sp, coro_drv_sp);
        }
        if (_t == 0x05BEu) {
            main_entry();
            coro_returned++;
            for (;;) port_ctx_switch(&coro_game_sp, coro_drv_sp);
        }
#ifdef PORT_JUMP
        if (_t == (uint)PORT_JUMP) {       /* PROBE ONLY (Makefile PORTJUMP): start the fresh thread at a
                                            * scene routine, e.g. 0x1A406 = the crystal-ball scene, so a
                                            * headless run reaches a scene the tapes cannot (tick lottery) */
            extern uint FUN_0001a406(void);
            if (_t == 0x1A406u) { FUN_0001a406(); }
            coro_returned++;
            for (;;) port_ctx_switch(&coro_game_sp, coro_drv_sp);
        }
#endif
    }
#if defined(PORT_BOOT) && defined(PORT_PLAY)
    /* COLD BOOT ON THE COROUTINE'S STACK. Entering at frame_rotated resumes the
     * GAMEPLAY frame; the game's real loop is main_entry, which dispatches by mode,
     * so a build that re-enters frame_rotated every frame can never advance the
     * title or attract state machine. MEASURED 2026-10-03: 604 cold-booted frames
     * left TILEMAP_U and TEXT_U entirely zero -- cleared by the boot, never drawn
     * into -- while PAL_SH held a real S16 palette, because set_level_palettes runs
     * once during the boot and the mode dispatch that would draw never ran.
     *
     * FUN_0000040e falls into main_entry and never returns, so on this stack the
     * game's whole control flow persists across frames and port_frame_wait suspends
     * it wherever it stands. That is what a coroutine is FOR here -- not a speed
     * tweak, a requirement. */
    FUN_0000040e();
#else
    frame_rotated();
#endif
    coro_returned++;
    for (;;)
        port_ctx_switch(&coro_game_sp, coro_drv_sp);
}

/* WATERMARK, not a guess: the paint is 0xA5 and this counts how much of it survived.
 * An 8 KB allocation for a call graph nobody has measured is exactly the kind of
 * number this project has been burned by, so the rom reports it. */
static uint coro_stack_used(void)
{
    const volatile uint *p = (const volatile uint *)PORT_CORO_BASE;
    uint n = (uint)(PORT_CORO_TOP - PORT_CORO_BASE) / 4u, i;
    for (i = 0; i < n; i++)
        if (p[i] != CORO_PAINT)
            break;
    return (n - i) * 4u;      /* bytes from the deepest disturbed word to the top */
}
#endif

/* ---- 68K ENTRIES THAT RESET THE STACK (2026-10-24) ------------------------------
 *
 * Three routines open with `lea 0xffffff00,%sp` (prog68k 0x40E, 0x5BE main_entry,
 * 0x1E5A in the attract sequencer 0x1E54) and are reached by `jmp`: each attract step
 * ends `jmp 0x1e54` (e.g. 0x227C, 0x2286), so on the 68000 the sequencer's stack depth
 * is a constant. The decompiled C made every one of those a CALL, and gcc kept them
 * as calls (`jsr @r0` at FUN_00001e54+0x258 in sw315), so each attract step left a
 * ~80-byte frame on the coroutine stack. MEASURED on sw315 (ares, the 0xA5 paint over
 * .portstack): 688 bytes used at frame 3000, 1548 at 9000 -- 0.14 B/frame against a
 * 7680-byte stack, exhausted at ~52,000 frames; the master was found executing at
 * PC 0x06000268 with PR 0x02bb02bb, writing at 0x0602A7C0, 64 bytes BELOW
 * .portstack, at frame 57,000. That is the 15.4-minute attract hang.
 *
 * tools/cport_prep.py wraps those three functions (the pass "68K SP-reset entries")
 * so the wrapper calls port_sp_reset with the body. On the coroutine this does
 * what the lea does: the SH-2 SP goes back to a fixed root near the top of
 * .portstack and the body runs there; every frame above it is dead, exactly as the
 * 68000's are. Off the coroutine (a harness calling the routine directly) it is a
 * plain call. */
uint port_sp_resets;                  /* how many resets ran, for the reader */
#ifdef PORT_CORO
extern void port_sp_reset_jump(void *fn, uint a, uint b, uint c, uint d, uint e, void *root)
    __attribute__((noreturn));        /* sh_src/mars_start.s */
void port_sp_root_run(void *fn, uint a, uint b, uint c, uint d, uint e)
    __attribute__((noreturn, used));
void port_sp_root_run(void *fn, uint a, uint b, uint c, uint d, uint e)
{
    ((uint (*)(uint, uint, uint, uint, uint))fn)(a, b, c, d, e);
    coro_returned++;                  /* none of the three returns; say so if one does */
    for (;;)
        port_ctx_switch(&coro_game_sp, coro_drv_sp);
}
#endif
PORTCODE uint port_sp_reset(void *fn, uint a, uint b, uint c, uint d, uint e)
{
#ifdef PORT_CORO
    char here;
    if (&here >= PORT_CORO_BASE && &here < PORT_CORO_TOP) {
        port_sp_resets++;
        port_sp_reset_jump(fn, a, b, c, d, e, (void *)(PORT_CORO_TOP - 64));
    }
#endif
    return ((uint (*)(uint, uint, uint, uint, uint))fn)(a, b, c, d, e);
}
static uint  mainloop_waits;      /* yields seen this frame */
static uint  mainloop_frames;     /* frames completed, for the reader */
static uint  mainloop_yields;     /* total yields, for the reader */

/* THE REDIRECT TARGET for wait_frame_flag (0x397E). Not PORTCODE-noinline by
 * accident: the generated unit calls it from .portcode, so it has to be reachable
 * from there like every other handler.
 *
 * AT THIS ENTRY THE FIRST YIELD IS THE FRAME BOUNDARY. `frame_rotated` begins at
 * 0x0AE2 -- just after the `jsr 0x397E` at 0x0ADC that the arcade is suspended in --
 * so one pass is tail, loop head, body, and then that same wait again. There is no
 * prologue to run first, which is why the seed stays where it always was (in the
 * driver, before the call) instead of being deferred into the hook.
 *
 * Leaving by __builtin_longjmp into the driver's still-live frame is the one legal
 * use of a non-local exit; `frame_rotated` is a `do { } while` that never returns. */
/* ---- THE MULTI-FRAME WAIT -------------------------------------------------------
 *
 * wait_frame_flag ends in `dbf %d0,0x397E`, so ONE call waits D0+1 frames and the
 * caller sets D0. The decompiler lost D0 -- every call is a bare `wait_frame_flag()`
 * -- so the stub yielded exactly once everywhere, and the game raced through every
 * timed hold in the attract.
 *
 * DERIVED, NOT GUESSED. Of the 34 `jsr 0x397E` sites in prog68k.bin, 32 are preceded
 * by `moveq #0,%d0` (one frame, which the plain stub already matched) and exactly two
 * are not:
 *
 *     jsr @0x000BA0   303C 00F0   move.w #240,%d0   -> 241 frames
 *     jsr @0x000D02   303C 0078   move.w #120,%d0   -> 121 frames
 *
 * Those are the attract's holds. The port returned after one frame at both, which is
 * why its end-of-frame marker ran on to 0x0B08 every frame while the arcade's sat at
 * 0x0AE2 for ~590 frames -- the first real control-flow divergence between the two,
 * found by diffing the two marker paths rather than by reading the code.
 *
 * The call sites are identified by the game's OWN breadcrumb: it writes
 * `_DAT_fffffefe = <return address>` immediately before each call, and the return
 * address is the jsr's address + 6. So 0xBA0 -> marker 0xBA6 and 0xD02 -> 0xD08,
 * both confirmed present in the corpus directly above a wait_frame_flag() call.
 * cport_prep rewrites exactly those two. */
PORTCODE uint port_frame_wait(void);   /* defined just below */

PORTCODE uint port_frame_wait_n(uint frames)
{
    uint i;
    for (i = 0; i < frames; i++)
        port_frame_wait();
    return 0;
}

PORTCODE uint port_frame_wait(void)
{
    if (!mainloop_armed) return 0;          /* nobody is driving; behave as the stub */
    port_watch_out[153] = (unsigned int)__builtin_return_address(0);   /* MEASUREMENT: who waited */
    port_watch_hook(0x397eu);
#ifdef PORT_FFWATCH
    { byte _a = ((volatile byte *)WRAM_BASE)[1], _b = ((volatile byte *)WRAM_BASE)[0x1822];
      byte _pa = (byte)(port_watch_out[176] >> 8), _pb = (byte)port_watch_out[176];
      if (_a == 0xFFu && _pa != 0xFFu) { port_watch_out[164]++; port_watch_out[165] = ((uint)((volatile byte *)WRAM_BASE)[0x3020] << 8) | ((volatile byte *)WRAM_BASE)[0x3021]; }
      if (_b == 0xFFu && _pb != 0xFFu) { port_watch_out[172]++; port_watch_out[173] = ((uint)((volatile byte *)WRAM_BASE)[0x3020] << 8) | ((volatile byte *)WRAM_BASE)[0x3021]; }
      port_watch_out[176] = ((uint)_a << 8) | _b; }
#endif

    /* ---- CLEAR THE FRAME LATCH, WHICH THE REAL FUNCTION'S FIRST INSTRUCTION DOES.
     *
     *     397e: 4238 f01c   clr.b 0xfffff01c      ; clear
     *     3982: 4a38 f01c   tst.b 0xfffff01c
     *     3986: 67fa        beq.s 0x3982          ; spin until IRQ4 sets it
     *     3988: 51c8 fff4   dbf   %d0,0x397e      ; and again, D0+1 frames
     *     398c: 4e75        rts
     *
     * (roms/altbeast/prog68k.bin at 0x397E -- wait_frame_flag, the one function the
     * port replaces with this stub.) The clear and the spin are ONE handshake: the
     * main loop clears the byte, the vblank handler sets it, and the handler's
     * register publish is gated on finding it clear --
     *
     *     if (DAT_fffff01e == 0) { if (DAT_fffff01c == 0) { DAT_fffff01c = 1;
     *         ... *_DAT_fffff0ec = _DAT_fffff0f4;  ... } }
     *
     * Stubbing the wait kept the yield and dropped the clear, so the latch stuck at
     * 1 after the first frame and the publish never ran again.
     *
     * WHAT IT COST: the S16 tilemap PAGE ASSIGNMENT registers (0x410E80/82) are
     * published by that block. Stuck at zero, decode_pages hands the compose "page 0
     * everywhere" and it draws a blank screen -- every master compose counter read 0
     * against the line's 1394, with the correct source values (0xFFF0F4 = 0x0101,
     * 0xFFF0F6 = 0x5656) sitting in the port's work RAM, matching the MAME arcade
     * oracle exactly. The port had the right answer and no way to publish it.
     *
     * NOT fixed here: the `dbf` means one call can wait D0+1 frames and this stub
     * yields once. Separate, and it needs the register file, not this byte. */
    { extern byte DAT_fffff01c; DAT_fffff01c = 0; }   /* placed by layout.ld */

    mainloop_waits++;
    mainloop_yields++;
    mainloop_frames++;
#ifdef PORT_CORO
    /* YIELD. The driver resumes us from exactly here on the next frame, with every
     * live frame of decompiled 68K code still below us -- which is the whole point
     * of the coroutine and the one thing re-entry cannot do. */
    port_ctx_switch(&coro_game_sp, coro_drv_sp);
    port_watch_hook(0x397fu);   /* MEASUREMENT: resumed inside the game */
    return 0;                               /* resumed: the game's loop goes on */
#else
    __builtin_longjmp(mainloop_exit, 1);    /* work RAM is the answer */
    return 0;                               /* not reached */
#endif
}

/* ---- YIELD WITHOUT CLEARING THE LATCH (PORT_BLANKLATCH) -------------------------
 *
 * The blank hold below needs to burn vints the way the ARCADE burns them inside a
 * scene load, and that is not what port_frame_wait does. The arcade's main loop is
 * suspended INSIDE the load: it never reaches wait_frame_flag, so 0xFFF01C stays
 * SET, so every IRQ4 takes its `latch set -> bump 0xFFF144 and do nothing' branch
 * and the game's own frame clock at 0xFFF020/21 PARKS. port_frame_wait clears the
 * latch on entry -- it is wait_frame_flag -- so each held vint publishes and
 * advances, and the hold costs the arcade's 21 frames of wall time while handing
 * the game 21 frames of clock the arcade never gave it.
 *
 * MEASURED 2026-10-20 over the whole attract, port game frame minus arcade game
 * frame at the same wall frame (the arcade dumps in tests/demo-b/arcade against
 * ares dumps of the same rom):
 *     boot  +8 | flat through X, T, E
 *     E->A install  +19  -> +27, then FLAT for 600 wall frames of demo A
 *     H install      +3  -> +30  (the arcade parks 1166-1168, we do not)
 *     H->B install  +19  -> +49, then FLAT for 600 wall frames of demo B
 *     B end          +3  -> +52
 * The rate is exactly right inside every scene; the lead is made entirely at the
 * transitions. The two +19s are the two load blanks sites 3/4 pace, and the arcade
 * parks its clock at 1494-1497 (gf 1469 four frames running) where we count 1514,
 * 1515, 1516, 1517.
 *
 * The +3s are the same thing without a blank: three vints the arcade's 68K misses
 * outright. Not addressed here.                                                  */
PORTCODE uint port_frame_hold(void)
{
    if (!mainloop_armed) return 0;          /* nobody is driving; behave as the stub */
    port_watch_hook(0x3980u);   /* MEASUREMENT: a HELD vint, distinct from a wait */
    mainloop_yields++;
    mainloop_frames++;
#ifdef PORT_CORO
    port_ctx_switch(&coro_game_sp, coro_drv_sp);
    port_watch_hook(0x3981u);   /* MEASUREMENT: resumed inside the hold */
    return 0;
#else
    __builtin_longjmp(mainloop_exit, 1);
    return 0;
#endif
}

/* ---- THE LOAD BLANK, PACED TO THE ARCADE (PORT_BLANKPACE) -----------------------
 *
 * The game blanks the display across a scene load by clearing bit 5 of its own
 * VIDEO_EN shadow 0xFFF018, runs clear_textram / clear_tileram /
 * clear_spriteram_and_pool and the scene setup, then sets the bit again. On the
 * arcade that load takes TWENTY-ONE FRAMES (measured against the arc6k corpus:
 * title->demo black 444-464, eye->demo 2 black 1478-1498). On ours it takes FOUR,
 * because the loop is WORK-paced and the same work runs about five times faster on
 * the SH-2 than on a bus-stalled 10 MHz 68000. face->eye blanks 4 on both and is
 * not a load.
 *
 * WHY THIS IS NOT A DISPLAY GATE. Holding only the SCREEN black would fix the
 * picture and leave the real defect: on the arcade the main loop is SUSPENDED
 * INSIDE the load for those 21 frames, which is why its scene timer 0xFFF02A parks
 * at 0 through the load. Finishing in 4 frames and then idling 17 with the main
 * loop running advances our timer 17 where the arcade's advances 0 -- which is the
 * ~10-frames-a-boundary lead that has turned up in four separate measurements
 * (scene boundaries, the five-slot emitter's phase, attract_parity's late rows and
 * temporal_metric's moving spans). So the hold BLOCKS: port_frame_wait from inside
 * the load's own call stack, which PORT_CORO suspends in place while the driver and
 * the renderer keep running.
 *
 * SELF-IDENTIFYING, NOT A LINE TABLE. Only a blank that covered a FULL clear is a
 * load; clear_spriteram_and_pool marks it. The blanks that cover no clear (and the
 * partial ones) are left exactly as they are, which is what keeps face->eye at 4.
 *
 * THE GATE IS THE TIMER, NOT THE BLANK: 0xFFF02A must PARK through the load as the
 * arcade's does and the boundary lead must go to ~0. The blank length is only the
 * symptom. */
#ifndef PORT_BLANK_FRAMES
#define PORT_BLANK_FRAMES 21u          /* measured, arc6k, both attract loads */
#endif
/* PACE AGAINST VINTS, NOT GAME YIELDS. The arcade's 21 is twenty-one VINTS. The
 * first cut counted mainloop_frames, which is game yields, and on a build whose
 * passes overrun a frame those are not the same thing -- measured 2026-10-17, a
 * natural 3-yield blank held to 21 YIELDS came out 32 display frames, 11 too long.
 * R60_DIAG[0] (r60_main.c, 0x26028E40) is the V-ISR's own vint count, a 32-bit
 * big-endian word, and is the clock the target is expressed in.
 * READ IT 32 BITS WIDE. A 16-bit read at 0x26028E40 takes the HIGH half, which is
 * zero all run long.
 * BLANK_MAX_SPIN is the backstop: on a build where that counter does not advance
 * (it reads 0 all run long on a pre-merge tree) the pacing is LOST rather than the
 * attract hanging inside a load. It tripped exactly that way on 2026-10-17 before
 * the merge, which is how the stale-renderer problem was found. */
#define PORT_VINTS     (*(volatile unsigned int *)0x26028E40u)
#define BLANK_MAX_SPIN 60u
static uint blank_on_vint;             /* PORT_VINTS when the blank went on */
static uint blank_on_frame;            /* mainloop_frames when the blank went on (reader) */
static int  blank_active;              /* a blank is open right now */
static int  blank_saw_load;            /* a full clear ran INSIDE this blank */
uint port_blank_paced;                 /* frames added, for the reader */
uint port_blank_loads;                 /* load blanks seen, for the reader */
volatile uint port_tile_load_serial;   /* completed large tilemap rectangle writes, including loads without a video blank */
/* THE SITE TABLE. cport_prep numbers the blank-OFF sites 0.. in source order and
 * passes the index in, because the self-identifying rule FAILED: 19 of the 21 sites
 * follow a full clear, so face->eye marked itself a load like every other and got
 * paced to 38 frames against the arcade's 4 (measured on sw186). Only the sites that
 * ARE the arcade's 21-frame loads get a target; 0 means leave the blank alone.
 * PORT_BLANKPROBE builds with an all-zero table and records the vint at which each
 * site fired, which is how the two load sites are identified in the first place. */
#define PORT_BLANK_SITES 32u
/* VOLATILE OR LTO DROPS THEM: nothing in the program READS these, they exist for the
 * dump, and a plain write-only global is eliminated. Found 2026-10-17 when nm had no
 * symbol for the array and the dump read a junk address. */
volatile uint port_blank_at[PORT_BLANK_SITES];
volatile uint port_blank_len[PORT_BLANK_SITES];   /* vints the blank lasted */
volatile uint port_blank_hits[PORT_BLANK_SITES];  /* times this site ended a blank */
volatile uint port_blank_last;
static const unsigned char blank_target[PORT_BLANK_SITES] = {
#ifdef PORT_BLANK_TABLE
    PORT_BLANK_TABLE
#else
    /* MEASURED, not guessed. The probe build (every blank-off recorded with its
     * length and the vint it ended on) says, by frame 1700 with pacing off:
     *     site  1   1 hit   ended vint    1   length 1    boot
     *     site  3   2 hits  ended vint 1449  length 4    THE LONG CUTS
     *     site  4   2 hits  ended vint 1449  length 4    same blank, closed twice
     *     site 19   1 hit   ended vint 1133  length 3    face->eye
     * Sites 3 and 4 end at the SAME vint with the same length, so they close ONE
     * blank; both carry the target and the second finds the hold already served,
     * because blank_on_vint only moves at blank-ON. Two hits each is the two long
     * cuts (title->demo and eye->demo 2), which are the same code path twice --
     * and which is why the arcade blanks 21 at BOTH of them.
     * SITE 19 IS LEFT ALONE: ours is 3 vints against the arcade's 4, and face->eye
     * is not a load. Pacing it to 21 was the bug in the self-identifying rule. */
    [3] = 21, [4] = 21,
#endif
};

PORTCODE byte port_blank_on(byte v)
{
    blank_on_frame = mainloop_frames;
    blank_on_vint  = PORT_VINTS;
    blank_active   = 1;
    blank_saw_load = 0;
    return (byte)(v & 0xDFu);
}

PORTCODE void port_blank_work(void)    /* clear_spriteram_and_pool ran */
{
    /* ONLY INSIDE AN OPEN BLANK. The clears also run outside one, and a flag left
     * armed there would pace the NEXT blank-off whatever it was -- there are 21
     * blank-off sites against 13 blank-on, so an unmatched off is normal. */
    if (blank_active)
        blank_saw_load = 1;
}

PORTCODE byte port_blank_off(byte v, uint site)
{
    /* RECORD EVERY BLANK, not only the ones the mark calls a load -- the mark was
     * wrong (it excluded the title cut and included face->eye), and a probe that
     * only sees what the rule already believes cannot correct the rule. */
    if (site < PORT_BLANK_SITES) {
        port_blank_at[site]   = PORT_VINTS;
        port_blank_len[site]  = (uint)(PORT_VINTS - blank_on_vint);
        port_blank_hits[site]++;
        port_blank_last       = site;
    }
    if (blank_saw_load) {
        uint want = (site < PORT_BLANK_SITES) ? blank_target[site] : 0u;
        port_blank_loads++;
        uint spin = 0;
        while (want && (uint)(PORT_VINTS - blank_on_vint) < want) {
            if (!mainloop_armed) break;          /* not driving: never spin */
            if (++spin > BLANK_MAX_SPIN) break;  /* the clock is not advancing */
            port_blank_paced++;
#ifdef PORT_BLANKLATCH
            /* LEAVE 0xFFF01C SET, as the arcade's suspended main loop does, so the
             * held vints advance no game clock. See port_frame_hold above. */
            port_frame_hold();
#else
            port_frame_wait();                   /* BLOCK, so the game's timer parks */
#endif
        }
    }
    blank_saw_load = 0;
    blank_active   = 0;
    return (byte)(v | 0x20u);
}

/* Run ONE rotated arcade frame. Work RAM must already hold the seed. */
volatile uint port_tick_done;   /* 0xFFF02A of the last COMPLETE game tick (check_demo.py) */
/* GAME FRAMES SINCE BOOT, as steps of the game's own blink clock 0xFFF020/21 (the clock
 * check_demo.py game_frame() reads). tools/parity_play.py --key-arcade keys the replayed
 * tape to this u16 with ares --input-gameframe, so a port running slower than 60 Hz takes
 * the arcade's route at the same GAME frames instead of the same wall frames: keyed by wall
 * frame the hero left the arcade's path 28 frames after the first enemy (2026-10-25). The
 * arcade side counts the same steps off its corpus timeline with the same rule: a step of
 * more than 4 frames is the boot-time init, not game frames, and counts 0. */
/* INITIALISED, NOT .bss: between the boot copy and the .bss clear (frames 5-8 on ares) a .bss
 * word reads stale image bytes (21394 here), ares took that as game frame 21394 and fired
 * every tape event up to it at boot. 0xFFFF is what ares reads as "counter not started". */
volatile unsigned short port_gf_count = 0xFFFFu;
#ifdef R60_LOCKPROBE
/* THE F154 WRITE RECORDER (diagnostic builds only, R60LOCKPROBE; CPORT_F154LOG makes cport_prep route
 * every write to the enemy count 0xFFF154 through port_f154_note and every zero-test through
 * port_f154_read). At four FPGA level-5 locks the footer read F154 = FF while no tick-end sample
 * ever saw it negative, so the writes are recorded where they happen: the first write that takes
 * the count from >= 0 to negative latches its site, old/new value, tick, and the object A6 points
 * at; the first zero-test that reads a negative count latches its site and tick. Game RAM is read
 * only. Layout: [0] hit [1..2] tick [3] site [4] old [5] new [6] A6 slot (0xFF = not a record)
 * [7..9] that record's handler [10] first negative read's site [11..12] its tick [13] F152 [14] writes. */
volatile uint8_t port_uf_latch[16];
void port_f154_note(unsigned site, unsigned char old)
{
    extern byte *_r_unaff_A6;
    const volatile byte *w = (const volatile byte *)WRAM_BASE;
    uint8_t nw = w[0x3154];
    port_uf_latch[14]++;
    if (!port_uf_latch[0] && old < 0x80u && nw >= 0x80u) {
        uint32_t off = (uint32_t)_r_unaff_A6 - (uint32_t)WRAM_BASE;
        port_uf_latch[1] = w[0x302A]; port_uf_latch[2] = w[0x302B];
        port_uf_latch[3] = (uint8_t)site; port_uf_latch[4] = old; port_uf_latch[5] = nw;
        if (off < 0x2000u) {
            const volatile byte *r = w + (off & ~0x7Fu);
            port_uf_latch[6] = (uint8_t)(off >> 7);
            port_uf_latch[7] = r[3]; port_uf_latch[8] = r[4]; port_uf_latch[9] = r[5];
        } else port_uf_latch[6] = 0xFFu;
        port_uf_latch[13] = w[0x3152];
        port_uf_latch[0] = 1u;
    }
}
unsigned char port_f154_read(unsigned site, unsigned char v)
{
    if (v >= 0x80u && !port_uf_latch[10]) {
        const volatile byte *w = (const volatile byte *)WRAM_BASE;
        port_uf_latch[11] = w[0x302A]; port_uf_latch[12] = w[0x302B];
        port_uf_latch[10] = (uint8_t)site;
    }
    return v;
}
#endif
#ifdef PORT_GODMODE
/* THE TICK-END MIRROR (measurement builds only). ares samples SDRAM at the frame start, which
 * lands inside the port's tick about half the time, so a timeline read straight off the work
 * RAM image holds half-updated records: the hero's x flickered 4196/4197/4208 sample to sample
 * where the arcade's held (2026-10-25). The bytes tools/parity_play.py's timeline reads are
 * copied here as the tick completes, beside port_gf_count, so every sample is a whole tick:
 *   +0x00 slot 0 (0xFFC000, 0x20)   +0x20 slot 40 (0xFFD400, 0x8)
 *   +0x28 0xFFE000 (0x30)           +0x58 0xFFF000 (0x160)        = 0x1B8 bytes */
volatile unsigned char port_tl_mirror[0x1B8];
#endif
static unsigned port_gf_prev = 0xFFFFFFFFu;
PORTCODE static void mainloop_one_frame(void)
{
    port_watch_hook(0x3333u);   /* MEASUREMENT: driver frame entry */
    mainloop_waits = 0;
    mainloop_armed = 1;
    /* THE 68K RE-LOADS ITS STACK POINTER ONCE PER FRAME and Ghidra dropped the
     * instruction: 0x62A is `lea 0xffffff00,%sp ; movew #8960,%sr`, inside this very
     * loop. The decompiled code spills through that pointer -- Ghidra calls it
     * register0x0000003c, which is 68K register space 0x3C, i.e. A7 -- including
     * `jsr` return addresses, which land in work RAM at 0xFFFFFEFC..0xFFFFFF00 and
     * are therefore part of the 16 KB diff. Defined by the generated defs.c. */
    port_reset_sp();
    port_play_dbg[3]++;        /* irq4_handler entries, counted by the DRIVER:
                                * CPORT_COUNT refuses irq4_handler (it does not
                                * recognise its 6-parameter signature), and whether
                                * the publish inside it runs is the open question. */
    /* THE ARCADE'S IRQ4 FIRES WHILE THE 68K SITS IN THE WAIT WE ARE RESUMING FROM.
     * The four-call body ran irq4_handler first for the same reason, and that
     * ordering is what the banked -43 was measured with. */
#if defined(PORT_IRQ4OWED) && (PORT_IRQ4OWED == 1)
    {   /* ONE CALL PER VINT THE MASTER OWED, not one per game frame. The handler decides
         * for itself: the first call finds the game's latch clear and publishes, the rest
         * find it set and only bump 0xFFF144, which is exactly what the arcade's does while
         * the 68K is busy in a teardown. See pp_vint_owed in sh_src/m_main.c. */
        extern volatile unsigned char pp_vint_owed;
        unsigned n = pp_vint_owed;
        pp_vint_owed = 0;
        while (n > 1) { irq4_handler(); n--; }
    }
#endif
    irq4_handler();
    port_watch_hook(0x3334u);   /* MEASUREMENT: after the 68K vint handler */
#ifdef PORT_CORO
    if (!coro_live) {
        volatile uint *p = (volatile uint *)PORT_CORO_BASE;
        uint n = (uint)(PORT_CORO_TOP - PORT_CORO_BASE) / 4u, i;
        for (i = 0; i < n; i++) p[i] = CORO_PAINT;
        coro_game_sp = port_ctx_make((void *)PORT_CORO_TOP, coro_entry);
        coro_live = 1;
    }
    port_watch_hook(0x3335u);   /* MEASUREMENT: before switching into the game */
    port_ctx_switch(&coro_drv_sp, coro_game_sp);
    port_watch_hook(0x3336u);   /* MEASUREMENT: the game yielded back */
    /* THE TICK IS COMPLETE HERE. An ares dump lands at the vblank, which can fall in
     * the middle of a tick (the SH-2 starts it whenever the master's loop reaches
     * pp_due, and catches up two per frame when behind), so a frame-end dump may show
     * tick N's counter with slots 0..k dispatched and k+1.. not. port_tick_done is the
     * value of 0xFFF02A for the last tick that ran to its wait; tests/demo-b/
     * check_demo.py accepts a frame only when it equals the counter in the dump. */
#ifdef PORT_KINDCENSUS
    /* ---- PROBE ONLY: WHICH ENEMY KINDS EVER SPAWN, PER LEVEL --------------------
     * Combat diverges between the two machines within seconds, so a record-by-record
     * diff of level 2 measures the divergence and not the port. What transfers is the
     * SET of handlers that ever run in the enemy band, slots 16-39 (the advisor's
     * call, 2026-10-21). One end-of-run dump cannot see it -- the band was empty at
     * frame 40000 of the first run while the arcade lists sixteen distinct handlers --
     * so accumulate it here instead, one scan a tick, keyed by the round in 0xFFF142.
     *
     * THE HANDLER IS THE LONGWORD AT +2, not the word at +4: 0x12F0A does not fit in
     * sixteen bits and the low word alone reads 0x2F0A.
     * port_kind_seen[round][i], terminated by a zero slot; port_kind_n[round] counts. */
    {
        unsigned _r = ((volatile byte *)WRAM_BASE)[0x3142] & 7u;
        unsigned _s;
        for (_s = 16; _s < 40; _s++) {
            const volatile byte *_p = (const volatile byte *)WRAM_BASE + _s * 0x80;
            if (!(_p[0] & 0x80)) continue;
            {   unsigned _h = ((unsigned)_p[2] << 24) | ((unsigned)_p[3] << 16)
                            | ((unsigned)_p[4] << 8)  |  (unsigned)_p[5];
                unsigned _k = (_r << 24) | (_h & 0xFFFFFFu);
                unsigned _i, _n = port_kind_n;
                for (_i = 0; _i < _n; _i++)
                    if (port_kind_seen[_i] == _k) break;
                if (_i == _n && _n < PORT_KIND_MAX) {
                    port_kind_seen[_n] = _k;
                    port_kind_n = _n + 1;
                }
            }
        }
    }
#endif
#ifdef PORT_HOLDLIVES
    /* ---- PROBE ONLY: HOLD THE LIVES COUNTER AT 3 (0xFFE018) ----------------------
     * tools/arcade_level_run.lua does exactly this from lua on the MAME side so an
     * unattended run reaches level 2; ares-headless cannot write memory mid-run, so
     * the port needs it compiled in to survive the same script. The game decrements
     * 0xFFE018 at altbeast_port.c's `DAT_ffffe018 = DAT_ffffe018 + -1` and ends the
     * game when it hits 0. Written once a TICK, after the game's frame, so the
     * decrement inside the frame still happens and only the game-over is prevented.
     * NEVER on a candidate: it is cheating, and the rate it buys is not the rom's. */
    ((volatile byte *)WRAM_BASE)[0x2018] = 3;
#endif
    port_tick_done = ((uint)((volatile byte *)WRAM_BASE)[0x302a] << 8) | ((volatile byte *)WRAM_BASE)[0x302b];
    {
        unsigned n = ((volatile byte *)WRAM_BASE)[0x3020], b = ((volatile byte *)WRAM_BASE)[0x3021];
        unsigned gf = (n >> 1) * 90u + (n & 1u) * 30u + ((n & 1u) ? 60u : 30u) - 1u - b;
        if (port_gf_count == 0xFFFFu)
            port_gf_count = 0;                                     /* the first tick */
        else if (port_gf_prev != 0xFFFFFFFFu) {
            unsigned d = (gf + 11520u - port_gf_prev) % 11520u;    /* 256 blink cycles */
            if (d <= 4u)
                port_gf_count = (unsigned short)(port_gf_count + d);
        }
        port_gf_prev = gf;
    }
#ifdef PORT_GODMODE
    {
        const volatile byte *w = (const volatile byte *)WRAM_BASE;
        unsigned i;
        for (i = 0; i < 0x20u; i++)  port_tl_mirror[i] = w[i];
        for (i = 0; i < 0x8u; i++)   port_tl_mirror[0x20u + i] = w[40u * 0x80u + i];
        for (i = 0; i < 0x30u; i++)  port_tl_mirror[0x28u + i] = w[0x2000u + i];
        for (i = 0; i < 0x160u; i++) port_tl_mirror[0x58u + i] = w[0x3000u + i];
    }
#endif
#else
    if (__builtin_setjmp(mainloop_exit) == 0)
        frame_rotated();
#endif
    mainloop_armed = 0;
}

#ifdef PORT_BOOT
/* ---- THE COLD BOOT. One frame, entered at the 68K RESET VECTOR.
 *
 * 0x400 holds `bra.w 0x40E`, so FUN_0000040e is power-on: it clears work RAM, clears
 * the text / tile / sprite layers, sets the level palettes and falls into main_entry
 * -- whose loop reaches wait_frame_flag, which is redirected to port_frame_wait and
 * longjmps back out here. So this call returns at the END of the game's first frame
 * and every frame after it is an ordinary frame_rotated.
 *
 * No irq4_handler first (there is no previous frame to service) and no seed: the
 * boot is the thing that BUILDS the state the seed used to supply. */
PORTCODE static void mainloop_boot_frame(void)
{
    mainloop_waits = 0;
    mainloop_armed = 1;
    port_reset_sp();
    if (__builtin_setjmp(mainloop_exit) == 0)
        FUN_0000040e();
    mainloop_armed = 0;
}
#endif /* PORT_BOOT */
#endif

/* THE FRAME BODY. `seed_upto` is vestigial: it existed when PORTMAIN entered at
 * 0x005BE and the seed had to be deferred past that function's prologue. At 0x0AE2
 * there is no prologue, so BOTH paths take work RAM as the caller left it. Kept as a
 * parameter rather than deleted at the call sites, because the one thing this driver
 * must never do is change two things between measurements. */
PORTCODE static void l2m_frame_body_seeded(int seed_upto)
{
#ifdef PORT_MAINLOOP
    (void)seed_upto;                 /* the caller has already seeded work RAM */
    mainloop_one_frame();
#else
    (void)seed_upto;
    irq4_handler();
    FUN_00003aae();
    FUN_0000144a();
    port_object_dispatch();
#endif
}

PORTCODE static void l2m_frame_body(void)
{
    l2m_frame_body_seeded(-1);
}

/* ---- THE FRAME'S OTHER INPUT: THE CONTROLLER AND THE DIPS -------------------
 *
 * A frame's inputs are the seeded work RAM PLUS the System 16 I/O state, and only
 * the first was ever supplied. FUN_00001366 (`bsr 0x1366`, in the loop head) reads
 * the ports straight:
 *
 *     DAT_ffffe028 = ~DAT_00c41003;      player 1
 *     DAT_ffffe0a8 = ~DAT_00c41007;      player 2
 *
 * so against a constant stub every input-derived cell is wrong by construction --
 * 9 of the 19 the rotated real frame got wrong (FULL-PORT.md 38). And the gate was
 * PAYING the hand-rolled four-call body for never reading the controller at all: it
 * leaves the seed values, which are already right.
 *
 * NOT CIRCULAR. These bytes are captured by tools/l2_oracle.lua from MAME's ioport
 * layer -- no bus access, no latch consumed -- and are an INPUT declared like the
 * seed, never read back out of the answer. They are CHECKED against the answer,
 * which is a different thing: ~io(N)[0xC41003] equals work RAM's 0xFFE028 at frame
 * N+1 for 239 of 257 frames (the other 18 have that write gated off) and player two
 * is 257 of 257.
 *
 * The four ports with no ioport behind them -- 0xC40001, 0xC41005, 0xC43001,
 * 0xC43007, which are display/watchdog and mapper, not inputs -- are recorded 0xFF
 * by the packer and so land on the same value the blanket fill gives them. */
extern const byte l2_io[];        /* sh_src/port_l2_io.s: 'L2IO' blob */
extern byte DAT_00c40001, DAT_00c41001, DAT_00c41003, DAT_00c41005,
            DAT_00c41007, DAT_00c42001, DAT_00c42003, DAT_00c43001, DAT_00c43007;

/* IN THE PACKER'S ORDER, and that order is the contract (tools/l2_io_pack.py). The
 * blob carries the 68K addresses too, and l2m_io_apply REFUSES if they disagree --
 * a silently re-ordered capture would feed the player-2 byte to player 1. */
static byte *const l2_io_cell[9] = {
    &DAT_00c40001, &DAT_00c41001, &DAT_00c41003, &DAT_00c41005, &DAT_00c41007,
    &DAT_00c42001, &DAT_00c42003, &DAT_00c43001, &DAT_00c43007
};
static const uint l2_io_a68[9] = {
    0xC40001u, 0xC41001u, 0xC41003u, 0xC41005u, 0xC41007u,
    0xC42001u, 0xC42003u, 0xC43001u, 0xC43007u
};
uint port_io_dbg[4];   /* [0] 'L2IO' seen, [1] frames, [2] ports, [3] frame applied */

/* CPORT_COUNT's array. The prep pass inserts `port_fn_count[i]++;` at the top of
 * each function named in that variable, in the order given; the driver publishes it
 * at port_l2m_out[248..255].
 *
 * WHY IT IS NEEDED ALONGSIDE PORTWATCH. The watch names the function whose span
 * CHANGED a cell -- so a function whose writes are all IDEMPOTENT on the current
 * state changes nothing and the watch reads zero spans, which is indistinguishable
 * from never having run. FUN_000039C8, the camera update, is exactly that case with
 * the camera frozen at the seed: every one of its writes is a no-op, and watching
 * any of them is circular. This answers "did it run" directly. */
volatile uint port_fn_count[8];
/* AND THE ORDER THEY RAN IN, packed into the same word.
 *
 * Counts alone cannot answer an ORDERING question, and ordering is what the player's
 * remaining defect turned out to be: the arcade's respawn chain reaches
 * FUN_00004DBA's `btst #4,($2c,A6)` while that byte is still 0 and only sets bit 4
 * 260 instructions LATER inside floor_collide, so it takes the state-4 branch. The
 * port had floor_collide set it FIRST and took the other branch (FULL-PORT.md 64).
 *
 * `port_fn_count[i]` is therefore `(first-entry sequence << 16) | entry count`: one
 * shared counter ticks on every instrumented entry, and each named function latches
 * its value the first time it is entered. The entry count is never near 65536 in one
 * frame, and if it were, the gate's own frame cost would have said so first. Packed
 * rather than given its own array because the result block's 0x400-byte dump is
 * exactly 256 words and they are all accounted for. */
volatile uint port_fn_seq;
/* CPORT_ARGLOG's ring lives at SDRAM scratch 0x0603A700 (w68.h): .bss has no room. */

PORTCODE static uint l2_io_u32(uint w)
{
    const byte *p = l2_io + w * 4u;
    return ((uint)p[0] << 24) | ((uint)p[1] << 16) | ((uint)p[2] << 8) | (uint)p[3];
}

/* Put arcade frame `f`'s I/O into the stub. f is the capture index, the same one
 * the seed and the delta chain use. Returns 0 and changes nothing on any
 * disagreement -- a wrong input is worse than a constant one, because it looks
 * computed. */
#ifdef PORT_LIVEPAD
#ifdef PORT_SOUND
static unsigned char port_snd_q[8]; static unsigned port_snd_w, port_snd_r;   /* sound commands queued for the 68K: COMM10 = 0x5A00 | cmd, acked to 0 */
#endif
volatile uint16_t port_live_p1;   /* probe: the raw P1 byte the 68K posted */
volatile uint port_live_svc;    /* VOLATILE, non-static: LTO folded the static (builder trap 2026-10-17). bit 8 = live pads seen; low byte = service bits pressed (coin1 b0, start1 b4, start2 b5) */
#ifdef R60_FRONTEND
/* THE OPTIONS SCREEN'S RESULT (md_src/testcart.c frontend_menu_screen, three words over
 * COMM12). The region and DSW1 decide the game's boot -- port_region_boot and the DIP latch
 * in FUN_0000040E -- so they are taken only before it has run (the power-on menu); DSW2 is
 * re-latched every frame (port_dips_latch, the title's glue) and changes from either menu. */
static unsigned frontend_round, frontend_dsw1, frontend_dsw2, frontend_configured, frontend_dsw1_set;
static unsigned port_game_booted;   /* set after the first game frame: the boot has latched */
PORTCODE void port_frontend_settings(unsigned packed, unsigned dsw1, unsigned dsw2)
{
    unsigned region = (packed >> 3) & 3u;
    frontend_round = (packed & 7u) < GAME_ROUNDS ? (packed & 7u) : 0u;
    if (!port_game_booted) {
        if (region < GAME_REGIONS_N) port_region = (byte)region;
        frontend_dsw1 = dsw1 & 0xFFu;
        frontend_dsw1_set = 1;
    }
    frontend_dsw2 = dsw2 & 0xFFu;
    frontend_configured = 1;
}
#endif
PORTCODE static void l2m_io_live(void)
{
    /* LIVE PADS (sw211): the 68K posts COMM2 = P1 << 8 | P2 (arcade active-low bytes) and
     * COMM8 = 0xA5 << 8 | service bits pressed, every vint. They override the capture. */
    uint c8 = MARS_SYS_COMM8;
#ifdef R60_FRONTEND
    if (frontend_configured) {
        extern void port_dips_latch(unsigned dsw2);   /* games/<title>/glue.c */
        DAT_00c42001 = (byte)frontend_dsw2;
        if (frontend_dsw1_set && !port_game_booted)
            DAT_00c42003 = (byte)frontend_dsw1;   /* read once, by the boot */
        port_dips_latch(frontend_dsw2);
    }
#endif
    if ((c8 & 0xFF00u) != 0xA500u) return;
    {   uint c2 = MARS_SYS_COMM2, svc = c8 & 0xFFu;
#ifdef PORT_STARTCOIN
        /* START AS COIN (Mike 2026-10-26: the Miyoo and the Analogue Pocket FPGA core do not see the wired 6-button pad,
         * so X/Y/Z/MODE never arrive and a 3-button pad can only coin with START+A). The common minimal-button MAME
         * mapping: START inserts a coin while the credits cannot pay for a start, and starts once they can. A 1P start
         * costs 2 credits (docs/proof/altbeast.c:155 `credits < 2` refuses, :160 `credits -= 2`) and DIPs 66/54 give
         * 3 credits for 2 coins, so a fresh machine takes START, START (3 credits), START (play). The choice is made at
         * the press EDGE and held to the release: a start that spends the credits below 2 must not turn the still-held
         * button into a coin. START is still passed to the game (the name entry and the continue countdown read it;
         * the game itself ignores a start it cannot afford). P2 START never coins (coin B is not wired). */
        {   static uint st_prev, st_coin;
            extern byte DAT_fffff000;                 /* credits, placed by layout.ld */
            uint st = svc & 0x10u;
            if (st && !st_prev) st_coin = (DAT_fffff000 < 2u);
            st_prev = st;
            if (st && st_coin) svc |= 0x01u;
        }
#endif
        port_live_p1 = (uint16_t)(c2 >> 8);
        DAT_00c41003 = (byte)(c2 >> 8);
        DAT_00c41007 = (byte)c2;
        DAT_00c41001 = (byte)~svc;            /* 0xC41001 active low: coin1 b0, test b2, service b3, start1 b4, start2 b5 */
        port_live_svc = svc | 0x100u;
    }
}
#ifdef PORT_TAPEPAD
/* THE IN-PORT TAPE (measurement builds, Makefile PORTTAPEPAD). ares injects input once per
 * wall frame, so a tick that runs second in a frame sees the first one's pad and a replayed
 * tape landed 0 or 1 game frames late by the frame's phase. Here the pad comes from the tape
 * table (sh_src/tape_pad.h, tools/tape_pad.py) at the START of each tick, indexed by
 * port_gf_count: an entry applies once the count reaches its key, which is the arcade's own
 * count before the pass that read it. Entries from TP_CAL_FROM on are play inputs and wait
 * for the offset the port measures at the first in-play tick TP_CAL_TICK after the start
 * sequence; the attract/coin transitions move the two machines' counts ~20 apart. */
#include "tape_pad.h"
static unsigned tp_i;
static int tp_off, tp_off_set;
PORTCODE static void l2m_io_tape(void)
{
    const volatile byte *w = (const volatile byte *)WRAM_BASE;
    int c = (port_gf_count == 0xFFFFu) ? 0 : (int)port_gf_count;
    if (!tp_off_set && tp_i >= (unsigned)TP_CAL_FROM && (w[0x3028] & 1u)
            && (((unsigned)w[0x302a] << 8) | w[0x302b]) == TP_CAL_TICK) {
        tp_off = c - (int)TP_CAL_S;
        tp_off_set = 1;
    }
    while (tp_i < (unsigned)TP_N) {
        int k = (int)tp_key[tp_i];
        if (tp_i >= (unsigned)TP_CAL_FROM) {
            if (!tp_off_set) break;
            k += tp_off;
        }
        if (k > c) break;
        tp_i++;
    }
    {   byte p1 = tp_i ? tp_p1[tp_i - 1] : 0xFFu, svc = tp_i ? tp_svc[tp_i - 1] : 0u;
        port_live_p1 = p1;
        DAT_00c41003 = p1;
        DAT_00c41007 = 0xFFu;
        DAT_00c41001 = (byte)~svc;
        port_live_svc = svc | 0x100u;
    }
}
#endif
#endif
PORTCODE static uint l2m_io_blob(uint f);
PORTCODE static uint l2m_io_apply(uint f)
{
    uint r = l2m_io_blob(f);
#ifdef PORT_LIVEPAD
    l2m_io_live();
#endif
    return r;
}
PORTCODE static uint l2m_io_blob(uint f)
{
    uint nf, np, i, stride;
    const byte *rec;
    port_io_dbg[0] = l2_io_u32(0);
    if (port_io_dbg[0] != 0x4C32494Fu)            /* 'L2IO' */
        return 0;
    nf = l2_io_u32(1); np = l2_io_u32(2);
    port_io_dbg[1] = nf; port_io_dbg[2] = np;
    if (np != 9u || f >= nf)
        return 0;
    for (i = 0; i < np; i++)
        if (l2_io_u32(3u + i) != l2_io_a68[i])
            return 0;                             /* the capture re-ordered: refuse */
    stride = (np + 3u) & ~3u;
    rec = l2_io + (3u + np) * 4u + f * stride;
    for (i = 0; i < np; i++)
        *l2_io_cell[i] = rec[i];
    port_io_dbg[3] = f;
    return 1;
}

PORTCODE static void l2m_fill_mmio(void) { port_mmio_reset(); }

/* ---- THE RESULT MAP, EXPLICIT. Anything added goes HERE first; the sixth slot
 * collision in this project's result arrays was mine, two hours ago.
 *   [0] 'FML2'  [1] frames available  [2] free-run done  [3] per-frame done
 *   [4] free-run ticks  [5] per-frame ticks  [6] canary  [7] downsample stride
 *   [8..39]    free-run curve, 32 downsampled points
 *   [40..71]   per-frame curve, 32 downsampled points
 *   [72..77]   free-run  min, max, sum, last, missed, spurious
 *   [78..83]   per-frame min, max, sum, last, missed, spurious
 *   [84..87]   free-run region split at the last frame
 *   [88]       free-run first wrong cell at the last frame
 *   [100..131] the UNCHANGED-BUT-WRONG cells on the last per-frame frame
 *   [132..163] the MISSED cells on the last per-frame frame
 *   [164][165] how many of each were recorded
 *   [166]      set only on REFUSAL: a frame with >255 delta records
 *   [167]      PORTZOOM: the frame that STEPPED (cells below are its)
 *   [168]      PORTZOOM: the window base, 0 = no zoom
 *   [169][170] PORTZOOM: the NET at the worst frame, and the control there
 *   [171..186] per-slot attribution: 4 x (sum, missed, spurious, controlsum)
 *   [187]      how many frames each slot was sampled over
 *   [188..191] PORTENDURE: 'ENDR', N requested, N done, ticks
 *   [192]      PORTMAIN: 'MAIN' -- the real main loop drove the frame
 *   [193][194] PORTMAIN: frames completed, total yields
 *   [195]      PORTMAIN: 'NOFR' -- free-run REFUSED (a longjmp cannot resume, so
 *              every frame re-runs the prologue and nothing accumulates)
 *   [196][197] 'L2IO' and the frame index, when the captured controller/dip state
 *              was applied to the MMIO stub. 0 means it was NOT -- read it before
 *              any input-derived cell.
 *   [198..200] PORTCORO: 'CORO', the coroutine stack watermark in bytes, and a
 *              count of times frame_rotated RETURNED (it is a loop, so never)
 *   [208..223] FREE-RUN PER-SLOT FIRST-WRONG FRAME, one byte per object slot,
 *              packed 4 per word big-endian. 0 = that slot was never wrong.
 *   [224..239] FREE-RUN PER-SLOT WRONG BYTES at the LAST frame, same packing,
 *              clamped at 255.
 *   [248..255] CPORT_COUNT: entry count per named function, in the order the
 *              variable names them (tools/cport_prep.py).
 *   [240..243] FREE-RUN WRONG-OFFSET-WITHIN-SLOT MASK at frame L2M_SLOTMASK_AT
 *              (default 8), 128 bits, bit n = byte n of SOME object record was
 *              wrong. [244..247] the same mask at the LAST frame.
 *
 *              WHY A MASK AND NOT A CELL LIST. The per-slot arrays showed the
 *              first error is a MARCH: slot 41 goes wrong at frame 2, 42 at
 *              frame 3, ... 53 at frame 14, each by EXACTLY 2 bytes. Thirteen
 *              instances of one defect, so the question is not which cells but
 *              WHICH TWO FIELDS of an object record -- and that is 4 words, not
 *              26. Frame 8 is sampled because only the marching slots are wrong
 *              there; by the last frame slots 0/16/17/48 have joined and would
 *              fill the mask.
 *
 *              WHY THESE TWO. The free-run curve does not DRIFT, it SATURATES:
 *              negative net for 67 frames, crossover at f73, then a plateau at
 *              ~195 wrong while the control falls to 122. A fixed plateau is a
 *              fixed set of cells gone permanently wrong -- about two object
 *              slots -- not accumulating noise, and an object slot is 0x80 bytes
 *              so offset>>7 names it. One run now says WHICH slots and WHEN each
 *              went, where before it took one endurance build per candidate
 *              frame.
 */
/* ---- PORT_PLAY: THE PORT PRODUCES THE DISPLAYED FRAME -------------------------
 *
 * Everything above this line SCORES the port. This does not: it runs the game.
 *
 * One call per displayed frame, from the master's frame body (sh_src/m_main.c, the
 * `pl_due` hook that PORT_LOAD's synthetic load used). The first call seeds work RAM
 * from the capture at PORT_PLAY's frame and resets the pseudo-registers; every call
 * after that feeds the capture's recorded controller bytes for the frame being run
 * and executes exactly one `frame_rotated`. No diff, no capture compare, no canary
 * sweep -- the output is the four video shadows the port wrote, which the renderer
 * draws under PORTVIDEO=1.
 *
 * STATE CARRIES, WHICH IS THE WHOLE POINT. This is the FREE-RUN path, not the
 * per-frame one: work RAM is seeded ONCE and the port's own frame k output is frame
 * k+1's input, so what appears on screen is the port's own evolving game and not a
 * re-stamped arcade state. Free-run is the curve that measures net-correct at every
 * sampled frame out to 250 (-100.1 B/frame against the control, 2026-10-03), so the
 * picture is expected to hold for seconds rather than frames -- and to drift
 * eventually, because 138 of those 250 frames still carry wrong bytes.
 *
 * io(k-1), NOT io(k), for the reason the free-run loop's own note gives: the frame
 * that READS the ports produces the state visible at the NEXT boundary.
 *
 * THE INPUT STREAM RUNS OUT. l2m_io_apply returns 0 past the capture's last frame
 * and leaves the previous values in place, so the pad simply holds whatever it was
 * last doing. The game keeps running; it is just no longer being driven. Not
 * clamped, because a held input is a truer continuation than a zeroed one and the
 * frame counter is published either way.
 */
#if defined(PORT_PLAY) && (PORT_PLAY > 0)
/* THE GAME'S OWN PC BREADCRUMB, ONE PER TICK.
 *
 * The decompiled program writes 0xFFFFFEFE before nearly every call it makes, so the
 * sequence of those values IS the call path. The arcade's can be sampled from MAME a
 * frame at a time; this records the port's, so the two can be diffed and the FIRST
 * control-flow divergence named instead of guessed at. 1024 ticks is ~17 s of attract,
 * which covers the boot and the whole eye sequence. */
volatile unsigned int port_mark_ring[64];    /* was 192: .bss sat exactly at 0x06019000 (rd42) and the 2026-10-04 re-export's 11 functions needed room */   /* (tick << 16) | marker, CHANGES only */
/* AND THE STATE MACHINE'S OWN TIMELINE: (tick << 8) | f031, one entry per CHANGE.
 * The arcade's is 0x00@7 0x04@15 0x08@19 0x0C@447 0x10@1168 0x14@1481, so a
 * side-by-side says WHERE the cold boot first falls behind -- which is the question
 * the attract gate cannot answer, because it roots every frame at the GAMEPLAY
 * entry and the attract's frame boundary is elsewhere. */
volatile unsigned int port_state_ring[32];
volatile unsigned int port_state_n;
/* SLOT 2's ANIMATION COUNTERS, one entry per CHANGE of +0x21.
 * The hold loop that state 0x10 sits in exits on `C0A1 == 6`; the arcade walks
 * +0x21 from 1 to 6 over ~313 frames (~52 frames a step) and leaves, the port
 * reaches 0x0A and jams. (tick << 16) | (+0x21 << 8) | +0x22. */
volatile unsigned int port_s2_ring[48];
volatile unsigned int port_s2_n;
volatile uint port_mark_n;
volatile uint port_play_dbg[8];   /* [0] 'PLAY'/'BOOT', [1] frames run, [7] ticks > 1 frame,
                                   * [2] capture frame, [3] io applied,
                                   * [4] LAST tick cost in FRT ticks,
                                   * [5] MAX tick cost, [6] summed cost >> 8 */

/* PALETTE-COLLAPSE DETECTOR (2026-10-04). rd5/rd6: PAL_SH held 985 colour words at
 * frame 490 and 1 at frame 495, with no counted palette writer running in between.
 * Which SIDE of the tick wipes it -- the game's frame (a 68K-port function) or the
 * master's render pass between frames -- is the question this answers. One sample
 * word per colour set (16 words apart) is enough to see a wipe.
 *   PALPROBE = port_watch_out[180..183] (PORTWATCH's block, idle in a play build;
 *   port_play_dbg is fully used and SDRAM scratch 0x0603A7xx is renderer-filled):
 *   [0] = tick of the collapse   [1] = port_fn_seq then
 *   [2] = 1 if it collapsed INSIDE the game frame, 2 if between frames   [3] = sets before */
#define PALPROBE (&port_watch_out[180])
static uint port_pal_sets(void)
{
    const volatile unsigned short *p = (const volatile unsigned short *)0x26027000u;
    uint s, n = 0;
    for (s = 0; s < 128u; s++) if (p[s * 16u + 1u] | p[s * 16u + 7u]) n++;
    return n;
}
static uint pal_sets_last;
#ifdef PORT_NOPALPROBE
static void port_pal_probe(uint tick, uint inside) __attribute__((unused));
#endif
static void port_pal_probe(uint tick, uint inside)
{
    uint n = port_pal_sets();
    PALPROBE[4] = tick; PALPROBE[5] = n; PALPROBE[6]++;   /* heartbeat: last tick, last sample, calls */
    {   /* the sprite draw-order lists (68K 0xFFEC80, 256 B): 0xFF = empty. [8] = after the
         * game frame, [9] = before it (between frames). rd10: zero at every frame end. */
        const volatile unsigned char *ol = (const volatile unsigned char *)WRAM_BASE + (GAME_WRAM_SPR_ORDER - SDRAM_WRAM_68K);
        uint k, ff = 0, z = 0;
        for (k = 0; k < 256u; k++) { if (ol[k] == 0xFFu) ff++; else if (ol[k] == 0u) z++; }
        PALPROBE[7 + inside] = (ff << 16) | z;
    }
    if (pal_sets_last >= 20u && n <= 2u && PALPROBE[2] == 0u) {
        PALPROBE[0] = tick; PALPROBE[1] = port_fn_seq;
        PALPROBE[2] = inside; PALPROBE[3] = pal_sets_last;
    }
    pal_sets_last = n;
}
PORTCODE void port_play_tick(void)
{
    volatile byte *w = (volatile byte *)WRAM_BASE;
    static uint pp_k, pp_started;     /* pp_started: the one-time setup ran */
    uint i;

    if (pp_started == 0u) {
        pp_started = 1u;
        port_mmio_zero();             /* a blank screen, not the 0xFF poison the
                                       * gate fills with: 0xFF is a tile index */
        port_reset_pseudoregs();
        dispatch_skip = -1;           /* no dispatch truncation in a play build */
#ifdef PORT_BOOT
#ifdef PORT_CORO
        /* The coroutine's own first switch-in IS the boot (coro_entry), so there is
         * nothing to call here: mainloop_one_frame creates the stack and enters. */
        /* THE DIP SWITCHES ARE NOT 0xFF.
         *
         * A cold boot read all nine I/O cells as 0xFF ("nothing pressed"), which is
         * right for the pads and WRONG for the two dip ports: FUN_0000040E latches
         * them at boot (`DAT_fffff01a = DAT_00c42003; DAT_fffff01b = DAT_00c42001;`)
         * and they steer the attract. MEASURED 2026-10-03 against a MAME capture of
         * the arcade's own I/O: 0xC42001 = 0x54 and 0xC42003 = 0x66, everything else
         * 0xFF. The port had 0xFF in both, so 0xFFF01A/1B read 0xFF where the arcade
         * holds 0x66/0x54 -- the port was booting a different machine. */
        /* PER REGION since 2026-10-30: altbeastj's 68K latches FF/FD under the same MAME
         * defaults (games/<title>/game.py REGIONS), and its attract and lives follow them. */
        {   static const byte dsw1[GAME_REGIONS_N] = GAME_REGIONS_DSW1;
            static const byte dsw2[GAME_REGIONS_N] = GAME_REGIONS_DSW2;
            for (i = 0; i < 9u; i++)
                *l2_io_cell[i] = (l2_io_a68[i] == 0xC42001u) ? dsw2[port_region]
                               : (l2_io_a68[i] == 0xC42003u) ? dsw1[port_region] : 0xFFu;
        }
        port_play_dbg[0] = 0x424F4F54u;   /* 'BOOT' */
#else
        /* COLD BOOT: no capture at all. The game clears its own work RAM, loads its
         * own layers and runs its own attract. Inputs idle at 0xFF (active low),
         * which is what md_main.c:7566 writes at reset. */
        for (i = 0; i < 9u; i++) *l2_io_cell[i] = 0xFF;
        port_play_dbg[0] = 0x424F4F54u;   /* 'BOOT' */
        mainloop_boot_frame();
        pp_k = 1u;
        port_play_dbg[1]++;
        port_play_dbg[2] = pp_k;
        return;                       /* the boot WAS this frame */
#endif /* PORT_CORO */
#else
        l2m_index();                  /* parse the capture blob */
        for (i = 0; i < WRAM_BYTES; i++) w[i] = l2_before[i];
        for (i = 1u; i <= (uint)PORT_PLAY && i <= l2m_n; i++) l2m_apply(w, i);
        pp_k = (uint)PORT_PLAY;
        port_play_dbg[0] = 0x504C4159u;   /* 'PLAY' */
#endif
    }

#ifndef PORT_BOOT
    port_play_dbg[3] = l2m_io_apply(pp_k);
#endif
#if defined(PORT_BOOT) && defined(PORT_LIVEPAD)
    l2m_io_live();                        /* the boot build never applied the capture; the live pads go in here, every tick */
#ifdef PORT_TAPEPAD
    l2m_io_tape();                        /* measurement: the tape overrides the live pads, by game frame */
#endif
#endif
    {
        /* WHAT THE TICK COSTS THE RENDERER. The master IS the renderer, and this
         * call sits in its frame body, so a port frame that overruns is not a slow
         * port -- it is a renderer that never gets to compose. FULL-PORT's opening
         * question ("can the translated game's instructions a vint live on the
         * master, on the slave, or must the work be split?") is decided here, and
         * only a number decides it. FRT16 wraps at ~91 ms, which is far longer than
         * any one frame, so a per-tick span is sound even though a summed one would
         * not be. */
#ifndef PORT_NOPALPROBE
    port_pal_probe(pp_k, 2u);         /* collapsed since last tick's post sample = between frames */
#endif
        uint t0 = FRT16();
        l2m_frame_body();
#ifdef R60_FRONTEND
        port_game_booted = 1u;        /* the first frame ran the boot: region and DSW1 are final */
#endif
#ifndef PORT_NOPALPROBE
        port_pal_probe(pp_k, 1u);     /* collapsed inside this frame */
#endif
        {
            uint dt = (uint)(unsigned short)(FRT16() - t0);
            port_play_dbg[4] = dt;
            if (dt > port_play_dbg[5]) port_play_dbg[5] = dt;
            port_play_dbg[6] += dt >> 8;
            if (dt > 11985u) port_play_dbg[7]++;   /* ticks longer than one NTSC frame
                                                     * (16.67 ms / 1.39 us per FRT tick) */
        }
    }
#ifdef PORT_BOOT
    /* ---- THE i8751, ON THE SH-2 ----------------------------------------------------
     *
     * The arcade's MCU talks to the 68K through three work-RAM mailboxes (US MCU
     * 317-0078; tools/game_altbeast.py): 0xFFF0C0 screen-sync busy, 0xFFF0C2 the
     * coin/service byte the MCU POSTS (inverted, 0 = nothing pressed), 0xFFF0C4 the
     * sound command the 68K posts and the MCU CONSUMES back to 0xFF. md_main.c
     * replicates the MCU for the 68K build ("Stage B"); it writes MD RAM, which a
     * port running on the SH-2 never sees. So a port-alone build had NO MCU.
     *
     * MEASURED 2026-10-04, with the real dips: 0xFFF0C4 held a posted command 0xB6
     * that nothing consumed (the arcade capture holds 0xFF, idle); by tick 1200 the
     * unconsumed queue had spilled 0xB6 into 0xFFF0C0..C2 as well, so MCU_COINS read
     * 0xB6 -- bits 2 and 7 set -- and irq4_handler's `do {} while (DAT_fffff0c2 & 4)`
     * spun forever waiting for an MCU to clear a TEST press that never happened. The
     * master SH-2 hung at 0x0238FBF6 for 183 million iterations (ares --profile), the
     * tick counter stopped at 849, and every per-frame function with it.
     *
     * This is the MD shim's per-vint half, verbatim in behaviour: post idle inputs to
     * MCU_COINS every frame (md_main.c:6729 `MCU_COINS = svc`), consume a pending
     * sound command back to 0xFF (md_main.c:5920-5924 -- logged, no Z80 yet), and the
     * busy flag exactly as it keeps it. Live coins/service come later with the pads. */
    {
        volatile unsigned char *mcu = (volatile unsigned char *)WRAM_BASE + (0xFFF0C0u - 0xFFC000u);
        static unsigned char mcu_busy;
        if (!mcu_busy && mcu[0]) mcu_busy = 1;        /* 68K set the busy high byte */
        else if (mcu_busy)       mcu_busy = 0;        /* no TEXT_SYNC here: one frame */
        if (mcu[4] != 0xFFu) {                        /* MCU_SND: consume the command */
            port_play_dbg[3] = (port_play_dbg[3] & 0xFFFF0000u) | 0xB600u | mcu[4];
#ifdef PORT_SOUND
            port_snd_q[port_snd_w & 7u] = mcu[4]; port_snd_w++;   /* to the 68K's music engine (md_src/md_sound.c) */
#endif
#ifdef PORT_SPEECH
            { extern void speech_cmd(unsigned); speech_cmd(mcu[4]); }   /* the slave's PWM speech lane (r60_speech.c) */
#endif
            mcu[4] = 0xFFu;
        }
#ifdef PORT_SOUND
        if (port_snd_r != port_snd_w && MARS_SYS_COMM10 == 0u) { MARS_SYS_COMM10 = (uint16_t)(0x5A00u | port_snd_q[port_snd_r & 7u]); port_snd_r++; }
#endif
#ifdef PORT_LIVEPAD
        mcu[2] = (port_live_svc & 0x100u) ? (unsigned char)(port_live_svc & 0xFFu) : 0x00u;   /* MCU_COINS: the MCU posts the service bits pressed (md_main.c old shim: MCU_COINS = svc) */
#else
        mcu[2] = 0x00u;                               /* MCU_COINS: nothing pressed */
#endif
#ifdef PORT_GODMODE
        {   /* measurement only (Makefile PORTGODMODE): hold P1 lives at 3 while in play, the
             * arcade oracle's GD_GOD rule, so a tape runs a whole level without dying */
            volatile unsigned char *f028 = (volatile unsigned char *)WRAM_BASE + (0xFFF028u - 0xFFC000u);
            volatile unsigned char *e018 = (volatile unsigned char *)WRAM_BASE + (0xFFE018u - 0xFFC000u);
            if ((*f028 & 1u) && *e018 < 3u) *e018 = 3u;
        }
#endif
#ifdef R60_FRONTEND
        if (frontend_round && (w[0x3028] & 1u) && w[0x3026] == 0u && w[0x314E] == 0u) {
            w[0x314E] = (byte)frontend_round;
            w[0x3142] = (byte)frontend_round;
        }
#endif
#ifdef PORT_FORM
        {   /* measurement only (Makefile PORTFORM=N): raise P1's TARGET form byte 0xFFC06B to N
             * while in play; the player handler's own form-change dispatch (0x4A6A, table
             * 0x6D90) then walks the current form up to it, so a tape meets the full-beast
             * paths (a 4th orb on a beast: Mike's sw297 rig freeze, 2026-10-22) without
             * having to hunt three wolves. */
            volatile unsigned char *f028 = (volatile unsigned char *)WRAM_BASE + (0xFFF028u - 0xFFC000u);
            volatile unsigned char *c06b = (volatile unsigned char *)WRAM_BASE + (0xFFC06Bu - 0xFFC000u);
            if ((*f028 & 1u) && *c06b < (unsigned char)(PORT_FORM)
#ifdef PORT_PROBE_ROUND
                && ((volatile unsigned char *)WRAM_BASE)[0x3142] == (unsigned char)PORT_PROBE_ROUND
                && !(((volatile unsigned char *)WRAM_BASE)[0x3026] & 0x10u)
#endif
               ) *c06b = (unsigned char)(PORT_FORM);
        }
#endif
#ifdef PORT_ROUND
        {   /* PROBE ONLY (Makefile PORTROUND=N, N = 1..4): start credited play at round N
             * (0 = level 1). The game's own round start does it: main_entry 0x5BE clears
             * the round index 0xFFF14E (0x5DA clrw) and waits one vint (0x640 jsr 0x397E),
             * then 0x670 sets the level byte 0xFFF142 = table 0x1CDA[0xFFF14E & 7]
             * (= 0,1,2,3,4) and loads the round; 0xBCA `addqb #1,0xFFF14E ; bra 0x62A`
             * is the real round advance. The window is P1 in play (0xFFF028 bit0, set at
             * 0x2D52 on START) with 0xFFF026 still 0 (bit7 is set at 0x73A once the round
             * is up; bit0 is the attract). Both bytes are written so a poke landing after
             * 0x670 is consistent too. tools/gameplay_dump.lua GD_ROUND=N is the same
             * poke on the arcade. Never on a candidate. */
            volatile unsigned char *f028 = (volatile unsigned char *)WRAM_BASE + (0xFFF028u - 0xFFC000u);
            volatile unsigned char *f026 = (volatile unsigned char *)WRAM_BASE + (0xFFF026u - 0xFFC000u);
            volatile unsigned char *f14e = (volatile unsigned char *)WRAM_BASE + (0xFFF14Eu - 0xFFC000u);
            volatile unsigned char *f142 = (volatile unsigned char *)WRAM_BASE + (0xFFF142u - 0xFFC000u);
            if ((*f028 & 1u) && *f026 == 0u && *f14e == 0u) { *f14e = (unsigned char)(PORT_ROUND); *f142 = (unsigned char)(PORT_ROUND); }
        }
#endif
#ifdef PORT_ENDCLEAR
        {   /* PROBE ONLY (Makefile PORTENDCLEAR=1): at a scroll lock, clear the enemy band.
             * The lock (0x9EA: camera 0xFFF132 >= 0xFFF13C, then 0xFFF140 counts down and
             * sets 0xFFF152) releases, and at the end of a level the boss transforms
             * (0x16C88), only when 0xFFF154 = 0 and no slot 16-39 is live. A tape cannot
             * aim, so an enemy that trails the hero holds the lock forever on either machine
             * (the arcade's kind-4 flames on level3_fightback*.csv, a kind-8 walker on the
             * port's runs). While P1 is in play and 0xFFF152 != 0: zero every live status
             * word in slots 16-39 and the counts 0xFFF154/0xFFF15D. The twin is
             * tools/gameplay_dump.lua GD_ENDCLEAR=1. Never on a candidate. */
            volatile unsigned char *w8 = (volatile unsigned char *)WRAM_BASE;
            if ((w8[0x3028] & 1u) && w8[0x3152] != 0u && w8[0x3154] != 0u
#ifdef PORT_PROBE_ROUND
                && w8[0x3142] == (unsigned char)PORT_PROBE_ROUND
#endif
               ) {
                unsigned _s;
                for (_s = 16u; _s < 40u; _s++)
                    if (w8[_s * 0x80u] & 0x80u) { w8[_s * 0x80u] = 0u; w8[_s * 0x80u + 1u] = 0u; }
                w8[0x3154] = 0u; w8[0x315D] = 0u;
            }
        }
#endif
        /* THE MCU SEEDS THE CREDIT COUNT. MEASURED 2026-10-04 on the oracle (MAME
         * altbeast, DIPs 0x54/0x66): 0xFFF000 reads 0xFF through frame 6 and 7 from
         * frame 7 on, with NO 68K write to it from the reset vector onward (MAME
         * watchpoint armed before frame 1) -- the i8751 writes it. The arcade then
         * shows "CREDITS 7" and spawns the credit object; the port showed "INSERT
         * COIN" and lacked slot 6 at the eye scene. Once, at the first tick. */
        /* RETRACTED 2026-10-04 (rd71): the 7 was MAME's NVRAM, not the MCU. The S16B
         * work RAM is battery-backed and MAME saves it to nvram/altbeast/nvram; the
         * oracle measured above had seven credits from an earlier session. A fresh
         * nvram directory reads 0xFFF000 = 0 from frame 7 (cell_timeline, 2026-10-04),
         * and the fresh-NVRAM arcade's table scene draws no "CREDITS 7" line (text row
         * 26 is bare where rd69 typed one). The factory board boots with zero credits;
         * restoring a saved count is the BATTERY SAVE card's job (STATE.md). */
    }
#endif
#if defined(PORT_BOOT) && defined(PORT_RESEED)
    /* ---- BOOT FIRST, THEN TAKE THE ARCADE'S STATE ------------------------------
     *
     * The two halves each work and neither is enough alone, measured 2026-10-03:
     *
     *   boot only   builds tile/text/palette RAM and DRAWS (18.3% of pixels), but
     *               its own cold-boot path diverges and it hangs at state 0x10.
     *   seed only   advances correctly THROUGH the transition to state 0x14 -- the
     *               one the cold boot never makes -- but renders black, because a
     *               work-RAM capture restores no video RAM.
     *
     * So: boot, let the game build its layers, then overwrite work RAM ONCE with the
     * arcade's captured state and let the coroutine carry on from there. This is the
     * gate's own model (position = where the game stands, state = the seed) applied
     * to a running picture rather than to a score. PORT_RESEED is the tick to do it
     * on; the capture is whichever L2DIR selected. */
    if (pp_k == (uint)PORT_RESEED) {
        uint i;
        l2m_index();
        for (i = 0; i < WRAM_BYTES; i++) w[i] = l2_before[i];
        port_play_dbg[7] = 0x52534544u;   /* 'RSED' -- the re-seed happened */
    }
#endif
    {
        /* slot 2 = 0xFFC080; +0x21 at 0xFFC0A1, +0x22 at 0xFFC0A2 */
        const volatile unsigned char *s2 = (const volatile unsigned char *)WRAM_BASE + (0xFFC0A0u - 0xFFC000u);
        static unsigned char s2_last = 0xFF;
        if (s2[1] != s2_last) {
            s2_last = s2[1];
            if (port_s2_n < 48u)
                port_s2_ring[port_s2_n] = (pp_k << 16) | ((unsigned)s2[1] << 8) | s2[2];
            port_s2_n++;
        }
    }
    {
        extern byte DAT_fffff031;   /* placed by layout.ld */
        static unsigned char st_last = 0xFF;
        if (DAT_fffff031 != st_last) {
            st_last = DAT_fffff031;
            if (port_state_n < 32u)
                port_state_ring[port_state_n] = (pp_k << 8) | (unsigned int)st_last;
            port_state_n++;
        }
    }
    {
        /* CHANGES ONLY. The marker repeats for hundreds of ticks while the game sits
         * in one loop, so logging every tick wasted 2 KB and blew the renderer's
         * region guard at 0x06019000. A change-log keeps the PATH -- which is the
         * question -- and covers the whole attract in 384 entries. */
        /* AT THE LIVE STACK POINTER, not at a fixed address.
         *
         * The breadcrumb is written BELOW A7 without moving it --
         * `*(undefined4 *)((int)register0x0000003c + -4) = 0x2212;` -- so its
         * address depends on stack depth. main_entry's copy of the code happens to
         * sit at a depth where it lands on 0xFFFFFEFC, which is why a fixed read
         * saw anything at all; frame_rotated's copy, which contains the state
         * switch, writes it somewhere else entirely and the fixed read was blind to
         * it. Reading through register0x0000003c (the port's emulated A7, set by
         * port_reset_sp) gets the live one wherever the stack happens to be. */
        extern uint register0x0000003c;
        static unsigned short mk_last;
        unsigned short mk = (unsigned short)(*(const volatile unsigned int *)
                                             ((register0x0000003c) - 4u));
        if (mk != mk_last) {
            mk_last = mk;
            if (port_mark_n < 64u)
                port_mark_ring[port_mark_n] = (pp_k << 16) | (unsigned int)mk;
            port_mark_n++;
        }
    }
#ifdef PORT_JUMP
    if (pp_k == (uint)PORT_JUMP_AT) { extern void port_irq_redirect(uint); port_irq_redirect((uint)PORT_JUMP); }   /* probe: see coro_entry */
#endif
    pp_k++;
    port_play_dbg[1]++;
    port_play_dbg[2] = pp_k;
}
#endif /* PORT_PLAY */

PORTCODE void port_l2m_run(void)
{
    volatile byte *w = (volatile byte *)WRAM_BASE;
    uint k, i, t0, first, reg[4], miss, spur, d, step, acc;
    uint amiss = 0, aspur = 0, bmiss = 0, bspur = 0;
    uint amin = 0xFFFFFFFFu, amax = 0, asum = 0, alast = 0;
    uint bmin = 0xFFFFFFFFu, bmax = 0, bsum = 0, blast = 0;

    for (i = 0; i < 4; i++) { port_l2m_canary_lo[i] = CANARY; port_l2m_canary_hi[i] = CANARY; }
    l2m_index();
    port_l2m_out[0] = 0x464D4C32u;         /* 'FML2' */
    port_l2m_out[1] = l2m_n;

    /* ---- ONE SEEDED FRAME, left in work RAM for a host-side diff. */
    if (L2M_SEEDAT) {
        port_l2m_out[188] = 0x53454544u;             /* 'SEED' */
        port_l2m_out[189] = L2M_SEEDAT;
        port_l2m_out[179] = (uint)(int)L2M_SKIP;
        l2m_fill_mmio();
        port_reset_pseudoregs();
        for (i = 0; i < WRAM_BYTES; i++) w[i] = l2_before[i];
        for (k = 1; k <= (uint)L2M_SEEDAT && k <= l2m_n; k++) l2m_apply(w, k);
        /* AFTER the blanket fill, so the captured ports win over 0xFF. The frame
         * being RUN is the one seeded to, so its own I/O is index L2M_SEEDAT. */
        l2m_io_apply((uint)L2M_SEEDAT);
        dispatch_skip = L2M_SKIP;
        if (L2M_WATCH) {
            port_watch_out[0] = 0x57415443u;          /* 'WATC' */
            port_watch_out[1] = L2M_WATCH;
            watch_sp = 0; watch_nhit = 0;
            watch_cell = WATCH_IS_TILE(L2M_WATCH) ? WATCH_TILE_PTR(L2M_WATCH)
                                                  : (w + ((uint)L2M_WATCH - 0xFFC000u));
        }
        t0 = FRT16();
        l2m_frame_body_seeded((int)(uint)L2M_SEEDAT);
        if (L2M_WATCH) {
            watch_cell = 0;
            port_watch_out[128] = watch_nhit;
            port_watch_out[166] = watch_nhit;
        }
        port_l2m_out[191] = (uint)(unsigned short)(FRT16() - t0);
        dispatch_skip = -1;
        /* WHAT THE CHAIN ACTUALLY HOLDS. The write-backs are in (3,647 references
         * in the unit) and the seed frame did not move, so publish the file rather
         * than reason about it: [180] D3, [181] D6 after the frame body. The arcade
         * at this frame ends slot 40 with D3=0x12 and D6=0xFA80. */
        port_l2m_out[180] = _port_reg[3];
        port_l2m_out[181] = _port_reg[6];
        port_l2m_out[182] = _port_reg_n;
        port_l2m_out[183] = port_zoom_dbg[0];
        port_l2m_out[184] = port_zoom_dbg[1];
        port_l2m_out[185] = port_zoom_dbg[2];
        port_l2m_out[201] = port_spawn_dbg[0];
        port_l2m_out[202] = port_spawn_dbg[1];
        port_l2m_out[203] = port_spawn_dbg[2];
        port_l2m_out[204] = port_spawn_dbg[3];
        port_l2m_out[205] = port_spawn_dbg[4];
        port_l2m_out[206] = port_spawn_dbg[5];
        port_l2m_out[186] = _port_reg[9];   /* A1 -- the arcade has 0x6F2C at the zoom call */
        /* A5, because the player's respawn hangs off it: the despawn needs
         * A4 = the word at A5+0x1C, and the arcade has A5 = 0xFFFFE000 (the player-1
         * record) at that call. Published as the SH-2 pointer the port holds, so
         * subtract WRAM_BASE to read it as a 68K address. FULL-PORT.md 62. */
        port_l2m_out[187] = _port_reg[13];
#ifdef PORT_MAINLOOP
        port_l2m_out[196] = port_io_dbg[0];  /* 'L2IO' if the capture was applied */
        port_l2m_out[197] = port_io_dbg[3];
        port_l2m_out[192] = 0x4D41494Eu;    /* 'MAIN' */
        port_l2m_out[193] = mainloop_frames;
        port_l2m_out[194] = mainloop_yields;
#ifdef PORT_CORO
        port_l2m_out[198] = 0x434F524Fu;    /* 'CORO' */
        port_l2m_out[199] = coro_stack_used();
        port_l2m_out[200] = coro_returned;
#endif
#endif
        for (i = 0; i < 8u; i++) port_l2m_out[248 + i] = port_fn_count[i];
        port_l2m_out[190] = 1;
        return;                                      /* work RAM IS the result */
    }

    /* ---- ENDURANCE: free-run N frames and LEAVE WORK RAM AT THE ENDPOINT.
     *
     * Built with PORTENDURE=N this REPLACES the curves. It costs no cart space for
     * deltas -- the host compares the dumped work RAM against the arcade's capture
     * at frame 9000+N -- so N is not bounded by .portdata or by the O(k^2) scorer,
     * and can be the repo's own 900-frame probe unit (15 s) instead of what the
     * delta chain fits.
     *
     * It also yields EVERY wrong cell at the endpoint; the in-rom collector holds
     * the first 32.
     *
     * IT REPLACES THE CURVES RATHER THAN FOLLOWING THEM because the curves and the
     * ablation passes re-seed work RAM dozens of times -- whatever they leave behind
     * is not an endpoint, and a dump must be unambiguous about which run made it. */
    if (L2M_ENDURE) {
        port_l2m_out[188] = 0x454E4452u;             /* 'ENDR' */
        port_l2m_out[189] = L2M_ENDURE;

        /* SELF-TEST THE SCORER FIRST, then free-run. Order matters and getting it
         * wrong produced two plausible wrong numbers in a row: whatever runs LAST
         * owns work RAM, and an ares --dump reports the END state. With the
         * self-test last, work RAM held expected(N) -- the capture itself -- and the
         * host diff read 2. With the one-frame ladder still running after, it held
         * one frame from the seed and the host diff read 241 against a control of
         * 236, which looks exactly like a port that nearly works.
         *
         * The self-test seeds work RAM to expected(N) by APPLYING the deltas (the
         * host proves that image IS the capture, 0 bytes differ) and scores it. The
         * only honest answer is 0. */
        /* the self-test and the in-rom score only mean anything for N within the
         * delta chain; beyond it the host diff is the instrument */
        port_l2m_out[79] = l2m_n;
        port_reset_pseudoregs();
        for (i = 0; i < WRAM_BYTES; i++) w[i] = l2_before[i];
        for (k = 1; k <= (uint)L2M_ENDURE && k <= l2m_n; k++) l2m_apply(w, k);
        port_l2m_out[76] = l2m_score(w, (uint)L2M_ENDURE, &first, reg, &miss, &spur);
        port_l2m_out[77] = (first == 0xFFFFFFFFu) ? 0xFFFFFFFFu : (0xFFC000u + first);
        port_l2m_out[78] = 0x53454C46u;               /* 'SELF' */

        /* now the endurance run, and it is LAST so work RAM is the endpoint */
        l2m_fill_mmio();
        port_reset_pseudoregs();
        for (i = 0; i < WRAM_BYTES; i++) w[i] = l2_before[i];
        acc = 0;
        /* FREE-RUN IS AVAILABLE AGAIN AT THIS ENTRY. The refusal here was written
         * when PORTMAIN entered at 0x005BE, whose PROLOGUE clears text/tile/sprite
         * RAM -- so re-entering per frame erased everything and nothing could
         * accumulate. `frame_rotated` begins at 0x0AE2, INSIDE the loop, and has no
         * prologue: re-entering runs tail, head, body, vsync, which is exactly one
         * more frame. State lives in work RAM and carries across by construction.
         * A coroutine would still be more faithful (it would preserve the SH-2 call
         * stack too) but it is no longer the precondition for measuring free-run. */
        for (k = 1; k <= (uint)L2M_ENDURE; k++) {
            /* PORTWATCH OVER THE LAST ENDURANCE FRAME ONLY.
             *
             * The watch used to be armed only inside the PORTSEED block, and that
             * made it blind to exactly the defect it was needed for: the free-run
             * cascade's first spurious spawn (slot 41) happens at FREE-RUN frame 2,
             * whose input is the PORT'S OWN frame-1 output, not the arcade's capture.
             * Armed on a seeded frame 1 the watch saw ZERO spans -- a true answer to
             * the wrong question. Arming it on the LAST endurance frame makes
             * PORTENDURE=N the frame selector: N is the frame watched, and the span
             * list is that frame's alone rather than every frame's merged. */
            if (L2M_WATCH && k == (uint)L2M_ENDURE) {
                port_watch_out[0] = 0x57415443u;      /* 'WATC' */
                port_watch_out[1] = L2M_WATCH;
                watch_sp = 0; watch_nhit = 0;
                watch_cell = WATCH_IS_TILE(L2M_WATCH) ? WATCH_TILE_PTR(L2M_WATCH)
                                                  : (w + ((uint)L2M_WATCH - 0xFFC000u));
            }
            t0 = FRT16();
            /* FEED THE FRAME ITS OWN CONTROLLER STATE. Without this a free-run is
             * being scored against a capture of a session WITH inputs while playing
             * with none, so it diverges by construction and the number says nothing
             * about the port. Frame k's record is index k; beyond the blob
             * l2m_io_apply returns 0 and leaves the last frame's values, which the
             * reader can see from [197]. */
            l2m_io_apply(k - 1u);   /* io(k-1): see the free-run loop's note */
            l2m_frame_body();
            acc += (uint)(unsigned short)(FRT16() - t0);
            if (L2M_WATCH && k == (uint)L2M_ENDURE) {
                watch_cell = 0;
                port_watch_out[128] = watch_nhit;
                port_watch_out[166] = watch_nhit;
            }
            port_l2m_out[190] = k;                   /* frames done, live */
        }
        port_l2m_out[191] = acc;
#if defined(PORT_MAINLOOP) && defined(PORT_CORO)
        port_l2m_out[198] = 0x434F524Fu;             /* 'CORO' */
        port_l2m_out[199] = coro_stack_used();
        port_l2m_out[200] = coro_returned;
#endif
        for (i = 0; i < 8u; i++) port_l2m_out[248 + i] = port_fn_count[i];
        port_l2m_out[72] = l2m_score(w, (uint)L2M_ENDURE, &first, reg, &miss, &spur);
        port_l2m_out[73] = miss; port_l2m_out[74] = spur;
        port_l2m_out[75] = (first == 0xFFFFFFFFu) ? 0xFFFFFFFFu : (0xFFC000u + first);
        return;                                      /* work RAM IS the endpoint */
    }
    /* THE CURVE IS DOWNSAMPLED TO 32 POINTS, not stored per frame. 256 frames of
     * per-frame counts would be 256 words per curve and the array does not have
     * them; the SHAPE is what the curve is read for, and min/max/sum/last carry
     * the rest exactly. */
    step = (l2m_n + 31u) / 32u;
    if (!step) step = 1u;
    port_l2m_out[7] = step;

    /* ---- FREE-RUN. Seed once; the port's own output is the next frame's input,
     * so an error at frame 3 is still there at the end. Pseudo-registers are NOT
     * reset between frames: on the 68K they carry across the frame boundary. */
    l2m_fill_mmio();
    port_reset_pseudoregs();
    for (i = 0; i < WRAM_BYTES; i++) w[i] = l2_before[i];
    /* PER-FRAME DELTAS SUMMED IN 32 BITS. FRT16 is a 16-bit free-running counter
     * and wraps at ~91 ms, so ONE span across a 256-frame curve is meaningless: the
     * same build reported 2.7 ms and 90.7 ms on two runs. Each FRAME is far below
     * the wrap, so per-frame deltas accumulate correctly. */
    acc = 0;
    /* FREE-RUN WORKS AT THE 0x0AE2 ENTRY. The refusal that used to sit here was for
     * the 0x005BE entry, whose prologue clears text/tile/sprite RAM so nothing could
     * accumulate; `frame_rotated` has no prologue and state carries in work RAM. */
    for (i = 0; i < SLOTS; i++) l2m_slotfirst[i] = 0;
#ifndef L2M_SLOT_PERFRAME
    l2m_slotmark = 1;               /* free-run ONLY: see l2m_slotfirst */
#endif
    for (k = 1; k <= l2m_n; k++) {
        t0 = FRT16();
        /* io(k-1), NOT io(k). The capture's own relation is that the frame which
         * READS the ports produces the state visible at the NEXT boundary --
         * `~io(N)[0xC41003]` equals work RAM's 0xFFE028 at frame N+1 (239 of 257
         * frames; the other 18 have that write gated off). Free-run frame k
         * transitions from the port's own frame k-1 to frame k, so the input it must
         * read is frame k-1's.
         *
         * The PER-FRAME loop already had this right (`l2m_io_apply(k - 1u)`, "the
         * frame being RUN is the one seeded to") and so did the seeded path; free-run
         * and endurance did not, so every free-run and endurance number before
         * 2026-10-03 was measured with the controller shifted one frame. */
        l2m_io_apply(k - 1u);
        l2m_frame_body();
        l2m_slotframe = k;
        d = l2m_score(w, k, &first, reg, &miss, &spur);
        acc += (uint)(unsigned short)(FRT16() - t0);
        amiss += miss; aspur += spur; asum += d; alast = d;
        if (d < amin) amin = d;
        if (d > amax) amax = d;
        if ((k - 1u) % step == 0u && (k - 1u) / step < 32u)
            port_l2m_out[8 + (k - 1u) / step] = d;
        port_l2m_out[2] = k;
        if (k == L2M_SLOTMASK_AT)
            for (i = 0; i < 4; i++) port_l2m_out[240 + i] = l2m_offmask[i];
        if (k == l2m_n) {
            port_l2m_out[88] = (first == 0xFFFFFFFFu) ? 0xFFFFFFFFu : (0xFFC000u + first);
            for (i = 0; i < 4; i++) port_l2m_out[84 + i] = reg[i];
            for (i = 0; i < 4; i++) port_l2m_out[244 + i] = l2m_offmask[i];
        }
    }
    l2m_slotmark = 0;
#ifndef L2M_SLOT_PERFRAME
    /* PACK BOTH PER-SLOT ARRAYS, 4 slots per word, slot 0 in the HIGH byte so a
     * hex dump reads left to right in slot order. */
    for (i = 0; i < SLOTS / 4u; i++) {
        port_l2m_out[208 + i] =
              ((uint)l2m_slotfirst[i*4+0] << 24) | ((uint)l2m_slotfirst[i*4+1] << 16)
            | ((uint)l2m_slotfirst[i*4+2] <<  8) |  (uint)l2m_slotfirst[i*4+3];
        port_l2m_out[224 + i] =
              ((uint)l2m_slotwrong[i*4+0] << 24) | ((uint)l2m_slotwrong[i*4+1] << 16)
            | ((uint)l2m_slotwrong[i*4+2] <<  8) |  (uint)l2m_slotwrong[i*4+3];
    }
#endif /* L2M_SLOT_PERFRAME: the per-frame curve owns this block instead */
    port_l2m_out[4] = acc;
    port_l2m_out[72] = amin; port_l2m_out[73] = amax; port_l2m_out[74] = asum;
    port_l2m_out[75] = alast; port_l2m_out[76] = amiss; port_l2m_out[77] = aspur;

    /* ---- PER-FRAME. Each frame starts from the ARCADE's state, so the score is
     * that frame's own difficulty with no inherited error. */
#ifdef L2M_SLOT_PERFRAME
    /* RESET, or the first-wrong column is the FREE-RUN's. The free-run loop runs
     * first and would otherwise have already stamped every slot, which turns the
     * one column that answers "does this slot own a defect" back into the column
     * that cannot answer it. Caught by the numbers looking identical to free-run. */
    for (i = 0; i < SLOTS; i++) l2m_slotfirst[i] = 0;
#endif
    acc = 0;
    {
        uint zoom = L2M_ZOOM, found = 0;
        int worstnet = 0;
        for (k = 1; k <= l2m_n; k++) {
            t0 = FRT16();
            /* WHICH FRAME GETS ITS CELLS RECORDED. Without a zoom it is the last
             * frame. With one it is the first frame in the window that STEPS, which
             * is the frame worth looking at. */
            if (zoom)
                l2m_collect = 0;        /* decided after the score, below */
            else
                l2m_collect = (k == l2m_n);
            l2m_nmiss = l2m_nspur = 0;
            port_reset_pseudoregs();
            for (i = 0; i < WRAM_BYTES; i++) w[i] = l2_before[i];
            for (i = 1; i < k; i++) l2m_apply(w, i);
            l2m_io_apply(k - 1u);   /* the frame being RUN is the one seeded to */
            l2m_frame_body_seeded((int)k - 1);
            /* PER-SLOT ON THE PER-FRAME CURVE TOO, UNDER PORTPFSLOT=1 -- and it
             * answers a question the free-run version cannot. Free-run per-slot says
             * "slot 16 is 3 bytes wrong at f146 and 61 at f250", which is consistent
             * with BOTH "its own defect, compounding" and "it inherits the player's".
             * The per-frame curve re-seeds from the arcade every frame, so there is no
             * inherited error at all: if slot 16 reads ~0 here it is purely downstream,
             * and if it reads large it owns a defect. Those need opposite work, which
             * is why guessing between them is not an option.
             *
             * It reuses the free-run block ([208..239]) rather than claiming new
             * slots: the result array is exactly the 256 words the 0x400 dump carries
             * and every one is accounted for. A build sets one or the other. */
#ifdef L2M_SLOT_PERFRAME
            l2m_slotmark = 1;
            l2m_slotframe = k;
#endif
            d = l2m_score(w, k, &first, reg, &miss, &spur);
#ifdef L2M_SLOT_PERFRAME
            l2m_slotmark = 0;
            if (k == l2m_n)
                for (i = 0; i < SLOTS / 4u; i++) {
                    port_l2m_out[208 + i] =
                          ((uint)l2m_slotfirst[i*4+0] << 24) | ((uint)l2m_slotfirst[i*4+1] << 16)
                        | ((uint)l2m_slotfirst[i*4+2] <<  8) |  (uint)l2m_slotfirst[i*4+3];
                    port_l2m_out[224 + i] =
                          ((uint)l2m_slotwrong[i*4+0] << 24) | ((uint)l2m_slotwrong[i*4+1] << 16)
                        | ((uint)l2m_slotwrong[i*4+2] <<  8) |  (uint)l2m_slotwrong[i*4+3];
                }
#endif
            bmiss += miss; bspur += spur; bsum += d; blast = d;
            if (d < bmin) bmin = d;
            if (d > bmax) bmax = d;
            if (zoom) {
                if (k >= zoom && k < zoom + 32u)
                /* THE WORST FRAME IN THE WINDOW, not the first to step. A step
                 * trigger only fires where the net JUMPS, and the net can be flat
                 * and high -- f201..f232 all sit near +45 -- so the window with the
                 * most to say would collect nothing. Tracking the maximum net always
                 * fires and picks the frame actually worth looking at. */
                if (k >= zoom && k < zoom + 32u) {
                    int nt = (int)(d - fcnt[k]);
                    if (!found || nt > worstnet) { worstnet = nt; found = k; }
                }
            } else if ((k - 1u) % step == 0u && (k - 1u) / step < 32u) {
                port_l2m_out[40 + (k - 1u) / step] = d;
            }
            acc += (uint)(unsigned short)(FRT16() - t0);
            port_l2m_out[3] = k;
        }
        port_l2m_out[168] = zoom;
        if (zoom && found) {
            port_l2m_out[167] = found;
            port_l2m_out[169] = (uint)worstnet;      /* SIGNED: the reader casts */
            port_l2m_out[170] = fcnt[found];
            l2m_collect = 1; l2m_nmiss = l2m_nspur = 0;
            port_reset_pseudoregs();
            for (i = 0; i < WRAM_BYTES; i++) w[i] = l2_before[i];
            for (i = 1; i < found; i++) l2m_apply(w, i);
            l2m_frame_body_seeded((int)found - 1);
            (void)l2m_score(w, found, &first, reg, &miss, &spur);
            l2m_collect = 0;
            port_l2m_out[164] = l2m_nspur;
            port_l2m_out[165] = l2m_nmiss;
        }
    }
    port_l2m_out[5] = acc;
    port_l2m_out[78] = bmin; port_l2m_out[79] = bmax; port_l2m_out[80] = bsum;
    port_l2m_out[81] = blast; port_l2m_out[82] = bmiss; port_l2m_out[83] = bspur;
    l2m_collect = 0;
    if (!L2M_ZOOM) { port_l2m_out[164] = l2m_nspur; port_l2m_out[165] = l2m_nmiss; }

    {   uint bad = 0;
        for (i = 0; i < 4; i++) {
            if (port_l2m_canary_lo[i] != CANARY) bad |= 1u;
            if (port_l2m_canary_hi[i] != CANARY) bad |= 2u;
        }
        port_l2m_out[6] = bad;
    }

    /* ---- PER-SLOT ATTRIBUTION BY LEAVE-ONE-OUT ABLATION.
     *
     * The first version of this dispatched ONE slot and compared to the control. It
     * was uninformative BY CONSTRUCTION and the measurement said so: all four slots
     * returned a net between -4.5 and -9.0, because dispatching one slot leaves the
     * other three's work undone and every row is dominated by that missing work
     * (~30 missed cells regardless of which slot ran).
     *
     * Ablation is the right shape: dispatch ALL slots EXCEPT one, and compare to the
     * full frame. The slot whose REMOVAL most improves the score is the one doing
     * the damage; a slot whose removal makes things worse is doing useful work.
     *
     * [171..186] four slots x (sum with that slot skipped, missed, spurious,
     * full-frame sum over the same sampled frames). */
    {
        static const signed char SL[4] = { 0, 16, 17, 40 };
        uint si, fullsum = 0, cnt = 0;
        /* the baseline over exactly the same sampled frames */
        for (k = 8u; k <= l2m_n; k += 8u) {
            port_reset_pseudoregs();
            for (i = 0; i < WRAM_BYTES; i++) w[i] = l2_before[i];
            for (i = 1; i < k; i++) l2m_apply(w, i);
            l2m_frame_body_seeded((int)k - 1);
            fullsum += l2m_score(w, k, &first, reg, &miss, &spur);
            cnt++;
        }
        port_l2m_out[187] = cnt;
        for (si = 0; si < 4u; si++) {
            uint ssum = 0, smiss = 0, sspur = 0;
            dispatch_skip = SL[si];
            for (k = 8u; k <= l2m_n; k += 8u) {
                port_reset_pseudoregs();
                for (i = 0; i < WRAM_BYTES; i++) w[i] = l2_before[i];
                for (i = 1; i < k; i++) l2m_apply(w, i);
                l2m_frame_body_seeded((int)k - 1);
                d = l2m_score(w, k, &first, reg, &miss, &spur);
                ssum += d; smiss += miss; sspur += spur;
            }
            dispatch_skip = -1;
            port_l2m_out[171 + si * 4 + 0] = ssum;
            port_l2m_out[171 + si * 4 + 1] = smiss;
            port_l2m_out[171 + si * 4 + 2] = sspur;
            port_l2m_out[171 + si * 4 + 3] = fullsum;
        }
    }

    /* ---- WHO WROTE IT. One pass per watched cell, from the arcade's own state at
     * the last frame, so the write observed is the one the gate scored. RESTORED
     * 2026-09-30: the rewrite above dropped these passes and left the hooks and
     * probe_sample() in place with nothing calling them, so port_watch_out read as
     * if the instrument had never run -- a silent loss of the instrument that found
     * the DAT_fffff142 width bug. */
    {
        static const uint WATCH[4] = { 0xFFEC80u, 0xFFECC0u, 0xFFED20u, 0xFFC800u };
        uint wi;
        port_watch_out[0] = 0x57415443u;             /* 'WATC' */
        for (wi = 0; wi < 4u; wi++) {
            port_reset_pseudoregs();
            for (i = 0; i < WRAM_BYTES; i++) w[i] = l2_before[i];
            for (i = 1; i < l2m_n; i++) l2m_apply(w, i);
            watch_sp = 0; watch_nhit = 0;
            watch_cell = w + (WATCH[wi] - 0xFFC000u);
            port_watch_out[1 + wi] = WATCH[wi];
            l2m_frame_body_seeded((int)l2m_n - 1);
            watch_cell = 0;
            port_watch_out[20 + wi * 3 + 0] = port_watch_out[136];
            port_watch_out[20 + wi * 3 + 1] = port_watch_out[137];
            port_watch_out[20 + wi * 3 + 2] = port_watch_out[138];
            port_watch_out[128 + wi]        = watch_nhit;
            if (wi + 1u < 4u)
                for (i = 136; i < 167u; i++) port_watch_out[i] = 0;
        }
    }
}



void port_l2m_run(void);

/* FREEZE AFTER A FEW SWEEPS. The driver runs every vint so the result survives to
 * the rig's first screenshot, but that makes an ares memory dump land at a random
 * point in the variant order -- so the work RAM it reads belongs to whichever probe
 * happened to be running, not to the headline result. Measured: a dump of a rom
 * reporting 207 showed 85 wrong bytes, which is the single-slot probe's score.
 * After FREEZE_AFTER sweeps the counters stop changing and the memory holds
 * variant 4's state, which is what port_l2_out[2..6] describes. */
#define FREEZE_AFTER 8

PORTCODE void port_l2_run(void)
{
    volatile byte *w = (volatile byte *)WRAM_BASE;
    uint i, v, vi, t0, _reg[4];

    port_l2_out[0] = 0x504C3220u;            /* 'PL2 ' */
    l2m_index();        /* the ladder scores through l2m_score now, which needs it */

    /* UNDER PORTENDURE THE LADDER MUST NOT RUN. It re-seeds work RAM and runs the
     * one-frame variants on EVERY vint for FREEZE_AFTER sweeps, so the state an
     * ares dump reads is variant 4's -- ONE frame from the seed -- and not the
     * endurance endpoint at all.
     *
     * Measured 2026-09-30, and it produced a plausible wrong number rather than an
     * obvious one: the host diff read 241 against a control of 236 and I reported
     * "net +5", when 241 is simply how far ONE frame from the seed sits from the
     * capture 256 frames later. The in-rom score of the real endpoint was 281. A
     * state that is nearly the control looks like a port that nearly works. */
    if (L2M_ENDURE || L2M_SEEDAT) {
        static int endure_done;
        if (!endure_done) { endure_done = 1; port_l2m_run(); }
        return;
    }
    {   /* PROBE: read the state jump table the object handlers dispatch through,
         * directly, in the driver. If [64] is not 0x0000EFD2 the port cannot see
         * the cart at all and the 208 missed calls are a memory-map fault, not a
         * dispatch-table gap. */
        extern char DAT_0000f556;
        const unsigned char *t = (const unsigned char *)&DAT_0000f556;
        port_l2_out[64] = ((uint)t[0] << 24) | ((uint)t[1] << 16)
                        | ((uint)t[2] << 8)  |  (uint)t[3];
        port_l2_out[65] = (uint)(unsigned long)t;
        port_l2_out[66] = ((uint)((const unsigned char *)ROM_BASE)[0xF556] << 24)
                        | ((uint)((const unsigned char *)ROM_BASE)[0xF557] << 16)
                        | ((uint)((const unsigned char *)ROM_BASE)[0xF558] << 8)
                        |  (uint)((const unsigned char *)ROM_BASE)[0xF559];
    }
    if (port_l2_out[8] >= FREEZE_AFTER)
        return;
    for (i = 0; i < 8; i++) {
        port_l2_canary_lo[i] = CANARY;
        port_l2_canary_hi[i] = CANARY;
    }

    /* ORDER: the headline variant (4, the full frame) runs LAST, so the work RAM
     * an ares dump reads is always the state that produced port_l2_out[2..6].
     * Otherwise the dump shows whatever the last per-slot probe left, and a
     * per-cell analysis of "the 207 wrong bytes" is really analysing a
     * single-slot run. */
    static const unsigned char ORDER[NVARIANT] =
        { 0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 4 };
    for (vi = 0; vi < NVARIANT; vi++) {
        v = ORDER[vi];
        uint diff = 0, first = 0xFFFFFFFFu, ticks;
        unknown_handlers = 0;
        port_l2_out[7] = 0;
        port_l2_out[15] = v;                 /* watchdog: the variant ENTERED */

        /* MMIO stub FIRST, work RAM second: if the two ever overlap, filling
         * after the seed destroys the frame state and the score measures the
         * overlap instead of the port. */
        port_mmio_reset();
        /* and the pseudo-registers, which persist between calls: re-seeding work
         * RAM alone left each variant's score depending on what ran before it
         * (191 vs 207 for the same sequence). */
        port_reset_pseudoregs();
        for (i = 0; i < WRAM_BYTES; i++)     /* re-seed: every variant gets the
                                              * same starting frame */
            w[i] = l2_before[i];
        /* and prove they do not overlap, rather than trusting the map: a stray
         * 0xFF inside the seeded frame would look like a port defect */
        if (w[0] != l2_before[0] || w[WRAM_BYTES - 1] != l2_before[WRAM_BYTES - 1])
            port_l2_out[22]++;

        port_l2_out[1] = 1;
        t0 = FRT16();
        run_variant(v);
        port_l2_out[1] = 5;
        ticks = (uint)(unsigned short)(FRT16() - t0);

        {   uint _m, _s, _f;
            diff  = l2m_score(w, 1u, &_f, _reg, &_m, &_s);
            first = _f;
        }
        port_l2_out[24 + v] = diff;          /* bytes wrong, per variant: 24..33,
                                      * and NOTHING else may use 24..39 */
        port_l2_out[21]     = ticks;         /* cost of the LAST variant run */
        port_l2_out[23]     = 0x5641524Du;   /* 'VARM': the variant block is valid */

        if (v == 4) {                        /* the best-guess sequence stays in [2..6] */
            port_l2_out[2] = diff;
            port_l2_out[3] = (first == 0xFFFFFFFFu) ? 0xFFFFFFFFu : (0xFFC000u + first);
            port_l2_out[4] = ticks;
            port_l2_out[5] = unknown_handlers;
            port_l2_out[6] = WRAM_BYTES - diff;
        }
    }
    {   /* report the canaries LAST: a surviving pair means nothing wrote through
         * the array during the sweep, and the numbers above can be believed. */
        uint bad = 0;
        for (i = 0; i < 8; i++) {
            if (port_l2_canary_lo[i] != CANARY) bad |= 1u;
            if (port_l2_canary_hi[i] != CANARY) bad |= 2u;
        }
        port_l2_out[22] = bad;               /* 0 = intact, 1 = low, 2 = high, 3 = both */
    }
    port_l2_out[8]++;                        /* sweeps completed */

    /* THE MULTI-FRAME CHAIN RUNS ONCE, on the first sweep, and LAST. It leaves
     * work RAM in its own state, so the remaining sweeps restore variant 4's --
     * which is what an ares memory dump is read against. */
    {   static int l2m_done;
        if (!l2m_done) { l2m_done = 1; port_l2m_run(); }
    }
}

#endif /* PORT_FULL */
