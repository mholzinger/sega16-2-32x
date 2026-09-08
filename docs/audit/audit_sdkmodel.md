# THE STANDARD 32X PROGRAMMING MODEL — SDK + d32xr audit

Sources read (all local, all full source):
- **marsdev SDK**: `/Users/mikeholzinger/src/marsdev/` — toolchain (`mars/m68k-elf`, `mars/sh-elf`, gcc 15.2), examples `examples/32x-skeleton/` (new) and `examples/32x-old-skel/` (Chilly Willy classic).
- **d32xr (Doom 32X Resurrection)**: `/Users/mikeholzinger/src/32x-builder/srcref/d32xr/` — complete checkout (crt0.s, marshw.c/h, marsnew.c, mars.h, src-md/crt0.s 3501 lines, src-md/main.c). The gold standard.
- **Other homebrew source local**: `/Users/mikeholzinger/src/32x-builder/` (Backrooms raycaster, sh_src/md_src, plus `D32XR_MINING.md` — two prior mining passes over d32xr with file:line cites, done for that project). OpenLara 32X was previously mined by the port itself (NOTES.md:2114-2161). No local Wolf32X tree found.
- **The port**: `/Users/mikeholzinger/src/sega16-2-32x/` — NOTES.md, DEVNOTES.md, sh_src/, md_src/.

---

## PART 1 — THE STANDARD MODEL

### 1.1 Boot: the fixed four-party handshake

Canonical shape (identical in d32xr and the SDK skeleton, both Chilly Willy lineage):

1. **Cart header**: 68K vector table pointing everything at the 0x3F0 Mars init blob (d32xr `crt0.s:19-26`), MD ROM header at 0x100, **Mars 68K exception jump table at 0x200** (`crt0.s:66-112`) — because after adapter enable, the adapter's own vector overlay routes 68K exceptions to fixed cart offsets 0x200+. Level-2/4/6 entries jump to fixed cart addresses **0x880900 (EXT), 0x880880 (HBlank), 0x8808C0 (VBlank)** (`crt0.s:90-94`).
2. **Standard Mars header at 0x3C0** (`crt0.s:114-124`): source/dest/size of the SH-2 module the BIOS copies ROM→SDRAM, plus Primary/Secondary jump addresses and **per-CPU VBR pointers** — the BIOS installs each SH-2's vector table for you.
3. **Sega security blob at 0x3F0** (`crt0.s:126-192`) — verbatim, position-locked.
4. **68K `_start` at 0x880800** (`src-md/crt0.s:133-163`): clear work RAM, copy .data, set SP to `0x00FFFFE0` (top of work RAM minus Z80-comm scratch), `init_hardware`, then `main()`.
5. **SH-2 `pri_start`/`sec_start`** (`crt0.s:336-436, 807-881`): clear all five 32X interrupt-flag latches, program the FRT (TIER=0, TOCR/OCRA — needed for the "bump ints" errata below), set stack (`pri_stack=0x0603F400`, `sec_stack=0x06040000`, `crt0.s:212-213`), purge+disable cache, clear .bss, **spin until the 68K writes "M_OK"/"S_OK" over COMM0/COMM4** (`crt0.s:384-396`), master lets the secondary run by clearing COMM4, then: set FM, set interrupt enables, lower SR mask, **purge cache and enable it (0x11 → 0xFFFFFE92)**, jump to C main (`crt0.s:398-413`).
6. **68K `init_hardware`** (`src-md/crt0.s:218-346`): VDP regs (H40/V28, tables), clear VRAM/CRAM/VSRAM, load debug font, probe controllers, **acquire Z80 bus, load the Z80 FM/VGM driver, release** (`crt0.s:292-331`), then wait for M_OK + S_OK (`crt0.s:336-341`), install `vert_blank` in the RAM vblank vector, display on, ints on.
7. **68K `do_main`** (`src-md/crt0.s:398-424`): sets **FM=1 (bit 15 of 0xA15100) — hands the 32X VDP to the SH-2s permanently** — and writes COMM0=0 to release the master (`crt0.s:420-424`).

**VRES (reset button)**: both SH-2s take the VRES interrupt to a handler that masks ints, resets the stack and jumps to warm-reset code that re-copies the ROM module to SDRAM (`crt0.s:781-801, 1317-1394`). The standard model survives the RESET button.

### 1.2 Interrupts: what is enabled and who consumes what

d32xr's steady state (`crt0.s:398-404, 850-854`):
- **Master: VBI + CMD enabled** (`mov #0x0A` → adapter int-enable reg, `crt0.s:401-402`), SR mask level 1 (`#0x10`).
- **Secondary: CMD only** (`mov #0x02`, `crt0.s:851-852`).
- Full SH-2 VBR jump tables route all 16 levels through one register-saving `pri_irq`/`sec_irq` dispatcher (`crt0.s:450-507`) with the **FRT-TOCR "bump ints" errata workaround on every entry** (write TOCR 0xE0/0xE2 and read back — `crt0.s:461-464`; OpenLara documents the same at ~11 sites), and **≥8-cycle padding (4 nops) between int-clear write and rts** so the int does not re-fire (`crt0.s:578-582`).
- **WDT interrupt (level 2)** = high-resolution timer: WDT overflow counter `mars_pwdt_ovf_count` (`crt0.s:744-775`), vector 65, priority set in IPRA (`marshw.c:301-302`), giving `Mars_GetWDTCount()` = (ovf<<8)|TCNT (`marshw.c:231-235`) — the profiler clock behind the in-game render-phase HUD (`marsnew.c:848-889`).
- **Secondary DMA1 interrupt (level 4)** for sound DMA chaining: `SH2_DMA_VCR1=66`, IPRA priority 4 (`marsnew.c:352-353`), handler clears TE and calls the sound mixer callback (`marshw.c:1076-1082`).

**The vblank is consumed by the master's VBI ISR** — `pri_vbi_handler` (`marshw.c:1044-1053`) does exactly two things: `mars_vblank_count++` and, if a new palette pointer was staged, one guarded CRAM upload. Everything else in the engine reads `mars_vblank_count` (`Mars_GetTicCount`, `marshw.h:72`) — the frame pacing primitive.

**CMD interrupt = 68K→SH-2 command doorbell.** The 68K asserts CMD INT (`0xA15102`) then talks a COMM0 handshake protocol (0xA55A ready / 0xFFFE exit) implemented in the crt0 IRQ handler itself (`crt0.s:598-689`); `pri_cmd_handler` dispatches on COMM0: 0xFF00 = controller push, 0xFF10 = DREQ DMA request, else user callback (`marshw.c:1055-1069`). This is how **the 68K interrupts the SH-2**, e.g. to deliver pad state every vblank — the SH-2 never polls the MD for input.

### 1.3 The frame model

- **Framebuffer flip is fire-and-forget**: `Mars_FlipFrameBuffers` toggles a shadow `mars_activescreen` (kept in an uncached alias, `marshw.c:84`) and writes FBCTL; the **hardware latches the flip at the next vblank** — waiting for `MARS_VDP_FBCTL & MARS_VDP_FS` to match is optional (`marshw.c:91-106`). `I_Update` (`marsnew.c:923-994`) flips **without waiting**, then busy-waits on the *tic count* (`ticsperframe` gate) — pacing is done on the vblank counter, and `I_RefreshCompleted()` (= flip latched, `marsnew.c:520-523`) is checked only where the code is about to write to the (new) backbuffer. A missed vblank costs one tic, never a mid-frame flip.
- **Line table**: first 0x100 words of each framebuffer are the per-scanline offset table; `Mars_InitLineTable` (`marshw.c:108-138`) builds it, points all lines ≥ height at one guaranteed-blank line, and implements **letterboxed 240p for PAL** (`Mars_InitVideo(-240)`, `marshw.c:237-274`; chosen in `C_Init`, `marsnew.c:424-429`).
- **Palette**: `Mars_SetPalette` only stashes a pointer (`marshw.c:145-148`); the actual 256-entry CRAM write happens **in the VBI handler**, guarded by `MARS_SYS_INTMSK & MARS_SH2_ACCESS_VDP` (FM check) and with **bit 15 (0x8000) set on every CRAM entry**, brightness/fade folded into the same pass (`marshw.c:154-184`).
- **Renderer**: hot functions and data are placed in **cacheable SDRAM** via `__attribute__((section(".sdata"), aligned(16), optimize(...)))` (`marshw.h:37`, `doomdef.h` ATTR_DATA_CACHE_ALIGN); `mars-ssf.ld` collects `.sdata` into the RAM region at 0x06000000. Framebuffer-touching loops pin `optimize("O1")` so GCC never substitutes builtins (`marsnew.c:95-97`).
- **Idle framebuffer memory is working memory**: `I_TempBuffer`/`I_WorkBuffer` carve level-setup scratch out of the *non-displayed* framebuffer (`marsnew.c:717-755`); CD file I/O stages through the framebuffer (`Mars_GetCDFileBuffer`, `marshw.c:930-933`); screen copies can be **parked in MD VRAM** via word-column store/load/swap commands (`marshw.c:791-808`, 68K side `cpy_md_vram`).

### 1.4 Master/Secondary split and inter-CPU communication

- The secondary runs a **COMM4 mailbox dispatcher**: spin on `MARS_SYS_COMM4 != MARS_SECCMD_NONE`, switch on a small command enum (wall prep / draw planes / draw sprites / sound DMA init / automap / sight checks / wipe), write COMM4=0 when done (`marsnew.c:334-411`; command enum `mars.h:34-59`). The master posts work with `Mars_R_SecWait()` + `MARS_SYS_COMM4 = cmd` inlines (`mars.h:82-194`).
- **Fine-grained work sharing uses the System-Register COMM ports, not SDRAM**: COMM6 is the shared work cursor for wall segs and visplanes (`mars.h:96-116`; `r_phase7.c` `pl_next` = COMM6) — high-frequency cross-CPU polling lands on dedicated adapter registers, off the SDRAM bus.
- **Cache coherency is explicit and line-granular**: `Mars_ClearCacheLine(addr)` = write to `addr|0x40000000` purge alias; `Mars_ClearCacheLines`; whole-cache purge = CacheControl(0)/CacheControl(CP|CE) (`marshw.h:75-90`). The master tells the secondary to purge via `MARS_SECCMD_CLEAR_CACHE` (`mars.h:84-88`). Bulk shared payloads stay **cached**; only tiny rovers/doorbells use the 0x20000000 cache-through alias (ring buffer, `mars_ringbuf.h:37-38`).
- **Per-CPU thread-local storage via GBR**: each CPU loads GBR with its own `mars_tls` struct (bank-switch page, validcount, column cache) at startup (`marsnew.c:37-52, 337, 471`) — how one codebase runs on both CPUs with per-CPU state and per-CPU SSF bank pages (`I_SetBankPage`, `marsnew.c:548-609`).

### 1.5 The 68K's role: I/O processor and the whole sound machine

The 68K after boot runs a **service main loop** (`src-md/crt0.s:420-490`):
1. **PWM DAC pump**: feeds sound samples to `MARS_PWM_MONO`, checking the FIFO-full bit, every loop iteration (`crt0.s:426-452`).
2. **Controller delivery** (`snd_ctrl`, `crt0.s:3137-3166`): each vblank sets `need_ctrl_int`; the main loop asserts CMD INT to the master and pushes both cached pad values through COMM0/COMM2 — SH-2 receives via `Mars_DetectInputDevices` (`marshw.c:725-765`) and thereafter `Mars_ReadController` is a **zero-wait read of a latched variable** (`marshw.c:767-782`).
3. **FM music**: `bump_fm` drives the Z80-resident VGM player; the Z80 does FM register writes, the 68K services its requests (`crt0.s:457-470`; Z80 driver protocol equates `crt0.s:8-67`).
4. **Command dispatch**: polls COMM0 (master requests) and COMM4 (secondary requests) against a **47-entry jump table** (`crt0.s:472-553`): SRAM read/write, music start/stop/volume, mouse, CD (Mega CD bridging, RoQ streaming, sfx), networking over joyport 2, MD-VDP debug text, bank-page switching, VRAM column store/load/swap, framebuffer↔AUX transfers.
5. **`dma_to_32x`** (`crt0.s:3168-3270`): the canonical **DREQ push** — CMD INT + COMM handshake, clear 68S, program DREQ dest/length, set 68S, then feed the FIFO 4 words at a time **polling the FIFO-full bit (0xA15107 bit 7) between bursts**, while the SH-2's `Mars_HandleDMARequest` (`marshw.c:877-917`) programs DMAC0 (source = `MARS_SYS_DMAFIFO`, DREQ edge mode) and a callback supplies the destination pointer.
6. **68K vblank ISR** (`vert_blank`, `crt0.s:2807-2865`): sets `need_bump_fm`/`need_ctrl_int` flags, **reads the pads** (`get_pad` `crt0.s:2867-2895`, with the `andi.w #0x0C00 / bne no_pad` disconnected-pad guard returning 0xF000), generates SCD IRQ2, handles VDP re-init latch. Real work stays in the main loop; the ISR latches state.
7. **RV discipline**: RV is set only in brief brackets around 68K accesses to cart-mapped resources (SRAM, mapper regs) — `set_rv`/`clr_rv` macros (`crt0.s:113-119`), with the loud warning in `marshw.h:100-105` that while RV=1 *nothing* on either SH-2 may read ROM (code, data, or interrupts).
8. **68K hot code runs from work RAM**, not ROM: the whole service loop is in `.data` (`crt0.s:349-351` "Put remaining code in data section to lower bus contention for the rom").

### 1.6 What the marsdev SDK itself establishes (vs d32xr)

The SDK's two skeletons are **much weaker than d32xr** and mostly notable for what they *don't* do:
- `32x-skeleton` (the one this port copied): SH-2 **int enables = 0x00 — no interrupts at all** (`sh_src/mars_start.s:275-281, 349-353`); MD side literally says `;// TODO: Interrupts crash... why?` and **polls the VDP status vblank bit** in its main loop (`md_src/md_main.c:73-77`), incrementing COMM12 as a frame tick; the SH-2 paces by watching COMM12 (`m_main.c:14-20 swapBuffers`) and flips FBCTL with a polled FS wait (`mars.c:160-172`). Pad reads are a **synchronous COMM0 round-trip** (`HwMdReadPad`, `mars.c:177-181`). Its `COLOR` macro omits the CRAM 0x8000 bit. Slave main = `for(;;);` (`s_main.c:3-5`).
- `32x-old-skel` (Chilly Willy's classic skeleton) has the full crt0 with the d32xr-style VBR tables and 0x0A int-enable lineage (`boot.s:211-213`), an `hw_32x.c` with the COMM command set, and a slave.c — i.e., the *old* skeleton is closer to the canonical model than the new one.
- 32x-builder's DEVLOG (cited at NOTES.md:119) records that marsdev's stock `mars_start.s` secondary-SH-2 boot is broken; both later projects boot on a repaired crt0.

So: **the established programming model is d32xr's, not the SDK's** — the SDK gives the toolchain and a bare shell; every serious 32X program (d32xr, OpenLara, Backrooms) converges on the Chilly Willy crt0 + "68K = I/O and sound processor, master = frame owner with VBI+CMD, secondary = mailbox worker" architecture.

---

## PART 2 — AVAILABLE BUT UNUSED (or reinvented) in sega16-2-32x

First, credit where due — the port **knows** this corpus intimately and adopted much of it:
- DIVU latency-hiding (`sh_src/sh2_asm.h:28` "Adapted from viciious/d32xr r_phase6.c:190-247"), cache-line purge alias (`sh2_asm.h:141` "Adapted from d32xr's marshw.h:75"), TAS mutexes (`sh2_asm.h:97-124`), `.ramtext` SDRAM-resident SH-2 code (m_main.c:39, NOTES.md:402-408 — the d32xr `.sdata` pattern), RV-as-pulse then RV pinned 0 with the 0x880000/0x900000 window model (NOTES.md:1220-1248 — "how Doom 32X runs"), 68K-owns-pads (NOTES.md:1250-1251), DREQ programming sequence learned from d32xr src-md/crt0.s:3168+ (NOTES.md:594-605) and hardened (per-word FIFO polling, DEVNOTES.md:14-15), CRAM ACCESS_VDP gate knowledge (NOTES.md:412), the FRT bump-ints/8-cycle-clear errata (NOTES.md:2153-2157 via OpenLara). The 68K-side constraint is also genuinely different: the MD 68K is not free — it runs the patched arcade game, so d32xr's "68K = service loop" does not transplant wholesale.

Now the gaps — mechanisms established by the SDK/d32xr that the port is *not* using (or rebuilt at higher cost):

### U1. SH-2 V-blank interrupt — OFF in the shipping build; vblank is re-derived by polling machinery
d32xr: master always runs with VBI+CMD enabled (`crt0.s:401-402`); the VBI handler is the tick source and palette-upload point (`marshw.c:1044-1053`); frame pacing = compare `mars_vblank_count` (`marsnew.c:973-977`).
Port: default int-enable is **0x00** (`sh_src/mars_start.s:346`); VBI exists only behind the `VISR_FLIP` experiment flag (`mars_start.s:332-336`, LOOP24) and CMD INT only behind retired probe flags. In its place the port built: MD-published V-counter heartbeats over COMM12 with a documented stale-read race (NOTES.md:1169-1191), an FRT-deadline "edge guard" for the flip (`m_main.c:3323-3389`), a vint-entry gate list under FMGATE (git 5bb9198), and the LOOP24 tear investigation whose finding — "tear = flip latch past vblank" — is exactly the event a V-ISR pins down for free. The port's own LOOP24 fix (`VISR_FLIP`: run the flip span from the master's V-ISR, `m_main.c:3195-3208`) *is* the d32xr model, still not default. **This is the single largest divergence from the established model: every "am I in vblank" decision is reconstructed from cross-CPU mailboxes instead of being an interrupt fact.**

### U2. The CMD-INT doorbell (68K→SH-2) — built, then retired; replaced with polling + spin protocols
d32xr: 68K asserts CMD INT for pad delivery, DREQ kickoff, everything (`src-md/crt0.s:3143, 3175`; SH-2 side `crt0.s:598-689`, `marshw.c:1055-1069`). Port: LOOP11 built CMDPROBE/CMDINT (md_main.c:1005-1006 cites d32xr src-md/crt0.s:3143), measured it three rounds, and retired it — "NEVER SHIP. The prize was always smaller than the price" (LOOP11.md:956, honored by LOOP23/24 per git 45c3b22). Legitimately measured-out for the *window pickup* use, but it means the port has **no asynchronous 68K→SH-2 signal at all**; everything rides polled COMM/SDRAM mailboxes with hand-tuned poll cadences (e.g. `s_main.c:27-67` preempt-blit mailbox, throttled idle meters `s_main.c:97-104`).

### U3. Vblank-deferred palette upload with the FM guard and the 0x8000 bit
d32xr: `Mars_SetPalette` stages, VBI uploads once per frame, checks `MARS_SH2_ACCESS_VDP`, sets 0x8000 on every entry, folds brightness (`marshw.c:145-184`). Port: knows the pattern (NOTES.md:412-413 "confirms the INTMSK ACCESS_VDP gate + upload-in-vblank-interrupt pattern for later stages") but applies CRAM **inside the render window** as part of the window schedule (m_main.c:23, "CRAM from maps[par]", W1 in the 3-window schedule NOTES.md:1183-1186), i.e. correctness is carried by the window/FM choreography rather than a self-guarding vblank ISR. The skeleton-inherited `COLOR` macro (`sh_src/mars.h:33`) still omits bit 15 — the port's real CRAM path builds entries elsewhere, but the SDK helper it ships is the known-buggy one (same nit D32XR_MINING.md:252 flagged for Backrooms).

### U4. WDT as the profiling clock
d32xr: WDT interrupt + overflow counter = wrap-free `Mars_GetWDTCount` (`marshw.c:231-235, 298-302`, `crt0.s:744-775`), `Mars_FRTCounter2Msec` conversion (`marshw.c:86-89`), live per-phase HUD (`marsnew.c:848-889`). Port: profiles with raw 16-bit FRT reads (`m_main.c:717-720 frt()`) and DIAG slots; all deadline math is modular-16-bit (`(uint16_t)(frt()-t0)` everywhere, e.g. m_main.c:3331) — works, but every span must stay under one wrap and there is no monotonic long-range clock on the SH-2 side.

### U5. Secondary-CPU sound DMA machinery + 68K PWM pump — the whole audio model, untouched
d32xr's audio stack: secondary SH-2 mixes into a ring and chains buffers off the **DMA1 interrupt** (`marsnew.c:352-353`, `marshw.c:1076-1082`, `Mars_Sec_InitSoundDMA` path `mars.h:146-152`), while the 68K pumps PWM DAC samples (`src-md/crt0.s:426-452`) and the **Z80 runs the FM/VGM driver** (`crt0.s:292-331` loader; request protocol `crt0.s:8-67`). The port has zero audio (`s_main.c:3` "no PWM audio yet"; queue order in memory: audio after cosmetics) — but the arcade game it hosts has a Z80+YM2151 score, and the established model already shows where each piece goes on this hardware (Z80 = FM driver the MD actually has, 68K = pump, secondary SH-2 + DMA1 INT = PCM). When the audio arc opens, this is the template; nothing in the port's current 68K vint budget accounts for it yet.

### U6. COMM-register work cursors for master/slave load balance
d32xr splits fine-grained work through COMM6 cursors and pixel-weighted split points (`mars.h:96-116`, r_phase7/r_phase8; D32XR_MINING.md:133-174 explains why the *System Register* location is what makes high-frequency polling free). The port's master/slave split is fixed-role (SYNC SDRAM mailbox, static band schedules, `m_main.c:22-34`) with a measured slave idle meter (14,427 polls/cycle at LOOP17, DEVNOTES.md:24-25; `s_main.c:89-104`). The port's stated reason for leaving COMM — "the MD stream owns COMM2..COMM10" (m_main.c:32-34) — has weakened since LOOP 8 retired the COMM stream (`s_main.c:57-66`: "COMM0 carries only the window command/ack"), so several COMM registers are again free for exactly the d32xr cursor pattern; the slave-idle number says there is capacity a dynamic cursor could harvest.

### U7. Fire-and-forget flip + "wait where it's free"
d32xr flips unconditionally and lets hardware latch it (`marshw.c:96-101`), waiting only before backbuffer writes (`marsnew.c:1000-1006`); OpenLara identically (NOTES.md:2126-2131, pageFlip/pageWait). The port cannot adopt this wholesale — its FB banks double as the game's staged tile/sprite/text RAM, so a flip has restore obligations (m_main.c:91-95) and a *missed* latch shows the wrong staging bank to the game — but the port's own notes identify the polled-pickup flip as the tear source (LOOP24) and the ISR-driven flip as the fix; the standard model says that fix (U1) is the default posture, not an experiment.

### U8. Line-table letterboxing / PAL support
`Mars_InitVideo(-240)` letterboxed PAL and the blank-line-pointer trick (`marshw.c:108-138, 241-253`). The port inherited the skeleton's NTSC-only 224-line table (`sh_src/mars.c:26-64`) and has no PAL story; for a "reusable S16→32X kit" (TOOLKIT.md ambition) the SDK already contains the mechanism.

### U9. MD VRAM / AUX as spare storage for the SH-2
`Mars_StoreWordColumnInMDVRAM` / Load / Swap and `Mars_StoreAuxBytes`/`Mars_LoadAuxBytes` (`marshw.c:791-808, 999-1014`) park framebuffer contents in otherwise-idle MD memory. The port uses the MD VDP *actively* (MDBGALL: MD draws the tile planes) — a stronger use of the same silicon — but the "MD memory as SH-2 swap space" primitive is unused and could serve the sprite-bake / cutscene arcs where SDRAM is the binding budget (region guard at 0x06019000).

### U10. GBR thread-local storage for dual-CPU code
d32xr's GBR-based TLS (`marsnew.c:44-52, 337, 471`) lets one function body run on either CPU with per-CPU caches/state. The port instead maintains parallel master/slave function variants and explicit SYNC-passed context (`slave_window_k(cmd)` etc.). Minor, but it is the established idiom for the "both CPUs run the same compose code on different halves" shape the port already has.

### Verdict ranking (impact if adopted, per the port's own measurements)
1. **U1/U7** — make VISR_FLIP-class vblank ownership the default; it closes the LOOP24 tear class by construction and deletes the heartbeat/edge-guard machinery that has produced three documented race bugs (NOTES.md:1169-1191, LOOP24).
2. **U5** — the audio arc has a complete, proven template sitting in srcref; budget the 68K vint for it now, not after the cadence work fossilizes.
3. **U6** — the slave idle meter says a COMM-cursor rebalance is the cheapest remaining compose win, and the original objection (COMM occupancy) expired in LOOP 8.
4. **U3/U4/U8/U9/U10** — hygiene/kit items: real for TOOLKIT.md, minor for Altered Beast today.
