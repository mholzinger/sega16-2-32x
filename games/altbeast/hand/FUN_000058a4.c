/* FUN_000058a4 -- three round-start records at 0xFFDD80/0xFFDE00/0xFFDE80 (slots 59-61),
 * called from 0x087A when the level byte 0xFFF142 is not 0 (levels 2-5).
 *
 * 0x58A4: A5 = 0xFFDD80; +0x24/+0x6C = two script pointers; `bsr 0x58E4` (fill the
 * record: status 0x8000, handler long 0x5928, ...); A5 += 0x80, the same for 0xFFDE00;
 * A5 += 0x80, the same for 0xFFDE80 -- and the third fill is a FALL-THROUGH into 0x58E4,
 * not a call. Ghidra inlined that third body into this one with its own symbols, and
 * declared `pcRamffffde82` a byte, so `move.l #$5928,2(a5)` stored 0x28 at 0xFFDE82 alone:
 * slot 61 lived the whole level as status 0x8000 with handler 0x28000000 (a dispatch miss
 * every tick), and the inlined copy also skipped the palette request on that record.
 * The script pointers are the 68K VALUES the arcade stores (0x70C6 ... 0x72D8), not
 * SH-2 addresses of the ROM copy. ares watch on sw324 + PORTROUND=2: frame 908, PC in
 * FUN_000058a4 wrote 0x8000 then byte 0x28 at 0x0602EA80/82. */
uint FUN_000058a4()
{
  ST32(W68PTR(0xFFDDA4u), 0x70c6u);
  ST32(W68PTR(0xFFDDECu), 0x7130u);
  FUN_000058e4((undefined2 *)(0xffffdd80));
  ST32(W68PTR(0xFFDE24u), 0x719au);
  ST32(W68PTR(0xFFDE6Cu), 0x7204u);
  FUN_000058e4((undefined2 *)(0xffffde00));
  ST32(W68PTR(0xFFDEA4u), 0x726eu);
  ST32(W68PTR(0xFFDEECu), 0x72d8u);
  FUN_000058e4((undefined2 *)(0xffffde80));   /* 0x58E0 falls through into 0x58E4 */
  return 0;
}
