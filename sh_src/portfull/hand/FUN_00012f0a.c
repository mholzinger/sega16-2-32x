/* FUN_00012f0a -- the level-2 snake's body segment (kind 7: head 0x12CBA + three 0x12F0A
 * parts, installed by hand/FUN_00012b08.c), from the 68K listing 0x12F0A-0x12FCC.
 *
 *   12f0a: movea.w $3c(fp),a0              the segment in front (head or previous part)
 *   12f0e: cmpi.b #4,$23(a0) ; beq 12f40   unless it is in state 4, follow it:
 *   12f16:   +0xC = +0x28 = a0+0x40 ; +0x10 = +0x2A = a0+0x10 ; +0x2E/+0x2F = a0's
 *            +0x68 = +0x5A
 *   12f40: jsr 100b0 ; bcc 12f58           carry = the segment in front is gone:
 *   12f48:   clr.w (fp) ; jsr 3bce ; jmp 3f04
 *   12f58: jump table 0x12FFC on a0's state +0x23:
 *            0, 3 -> 12f52 (jmp 3f04: hidden)
 *            1, 2 -> 12fac: +0x23 = state ; jsr 669c, d47e, 674e, d5a6 ; jmp 3d36
 *            4    -> 12f6c: on entering 4 set +0x23, dy 0x400, +0x1C 0x80, +0x1E 0x800 ;
 *                    jsr 3f72, 3f20, 669c ; +0xC = +0x40 ; +0x10 = +0x44 ; jmp 3d36
 *
 * WHY A HAND FILE (2026-10-25, Mike's sw324 play pass: "snakes have no bodies"). The
 * decompile inlined the 0x3D36 draw and read A1 (the frame's art address, 0x3C9E) and D3
 * (the art width, 0x3CF4) from the register-file pseudos captured at ENTRY
 * (`extraout_A1 = _r_extraout_A1`, `unaff_D3 = _r_unaff_D3`), not from sprite_frame_lookup
 * and zoom_scale_lookup: docs/audit/extraout_producers.json has no site in this function,
 * so the feed pass never fired. Every segment wrote a sprite record with pitch 0, address 0,
 * bank 0 (`xxxx xxxx 0000 0000 f0..` in the port's sprite table at 0xFFF800 where the
 * arcade has `0006 0d37 f4..` for frame 0x18F): drawn, invisible. The head (0x12CBA) is fed
 * and draws. Here the draw goes through sprite_build_depth_banded (0x3D14), whose 0x3D36
 * entry is reached by clearing +0x2C bit 4 around the call, as in hand/FUN_00013374.c. */
uint FUN_00012f0a()
{
  byte *fp = (byte *)_r_unaff_A6;
  byte *a0 = (byte *)W68(*(short *)(fp + 0x3c));          /* 12f0a */
  byte st;

  if (a0[0x23] != 4) {                                      /* 12f0e */
    short x = *(short *)(a0 + 0x40), y = *(short *)(a0 + 0x10);
    *(short *)(fp + 0x0c) = x;
    *(short *)(fp + 0x28) = x;
    *(short *)(fp + 0x10) = y;
    *(short *)(fp + 0x2a) = y;
    fp[0x2e] = a0[0x2e];
    fp[0x2f] = a0[0x2f];
    *(short *)(fp + 0x68) = *(short *)(fp + 0x5a);
  }
  FUN_000100b0((short *)a0);                                /* 12f40 */
  _r_unaff_A6 = fp;
  if (port_carry) {                                         /* 12f48 */
    *(ushort *)fp = 0;
    release_palette_slot();
    _r_unaff_A6 = fp;
    hide_object_sprite();
    _r_unaff_A6 = fp;
    return 0;
  }
  st = a0[0x23];                                            /* 12f5e */
  switch (st) {
  case 1:
  case 2:                                                   /* 12fac */
    fp[0x23] = st;
    FUN_0000669c();
    hitbox_a_expand();
    animation_frame_fetch();
    walk_player_slots();
    break;
  case 4:                                                   /* 12f6c */
    if (fp[0x23] != st) {
      fp[0x23] = st;
      *(short *)(fp + 0x1a) = 0x400;
      *(short *)(fp + 0x1c) = 0x80;
      *(short *)(fp + 0x1e) = 0x800;
    }
    clamp_y_velocity();
    integrate_position();
    FUN_0000669c();
    *(short *)(fp + 0x0c) = *(short *)(fp + 0x40);
    *(short *)(fp + 0x10) = *(short *)(fp + 0x44);
    break;
  default:                                                  /* 12f52: 0, 3 (and any other) */
    hide_object_sprite();
    _r_unaff_A6 = fp;
    return 0;
  }
  _r_unaff_A6 = fp;
  {   /* jmp 3d36: sprite_build_depth_banded WITHOUT its depth-band step (0x3D14-0x3D32,
       * taken only when +0x2C bit 4 is set) */
    byte c = fp[0x2c];
    fp[0x2c] = (byte)(c & ~0x10);
    sprite_build_depth_banded((uint)_port_reg[3], (undefined4)_port_reg[6], (undefined4)_port_reg[7]);
    fp[0x2c] = c;
  }
  _r_unaff_A6 = fp;
  return 0;
}
