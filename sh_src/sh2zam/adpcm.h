/* IMA/DVI 4-bit ADPCM decoding for the SH-2 (contract tag `adpcm-1`).
 * MIT; Copyright (c) 2026 SH2ZAM Contributors. Game-neutral: no title, bank or
 * output device is assumed. Not part of SH4ZAM; there is no SH-4 path.
 *
 * Contract
 *  - The decoder is the common bitwise IMA rule, including its truncations:
 *        step  = sh2z_ima_step[index]
 *        delta = (step >> 3) + (n & 4 ? step : 0) + (n & 2 ? step >> 1 : 0) + (n & 1 ? step >> 2 : 0)
 *        pred  = clamp(pred + (n & 8 ? -delta : delta), -32768, 32767)
 *        index = clamp(index + {-1,-1,-1,-1,2,4,6,8}[n & 7], 0, 88)
 *    and out[i] is pred after nibble i. sh2z_ima_ref_decode is that rule written
 *    out step by step and is the specification; sh2z_ima_decode must write the
 *    same bytes and leave the same state for every input.
 *  - Nibble order: nibble position p is the LOW nibble of src[p >> 1] when p is
 *    even and the HIGH nibble when p is odd (the order IMA/DVI streams use).
 *    `pos` is the first nibble decoded; `count` nibbles are decoded.
 *  - State: on entry pred must be in -32768..32767 and index in 0..88 (a state
 *    produced by these functions always is). On return it holds the state after
 *    the last nibble, so a stream can be decoded in pieces of any length.
 *  - count == 0 writes nothing and leaves the state unchanged. out may not
 *    overlap src or the state. Reentrant, no allocation, no global mutable
 *    state, no floating point, no libgcc calls, no hardware divider.
 *
 * Cost model: the fast path replaces the per-nibble step-table load, three bit
 * tests and two shifts with one lookup in a precomputed 89 x 8 difference table
 * (uint16, 1,424 bytes, the only table it reads), and clamps pred and the index
 * only on the side each nibble moves them. Bytes are consumed two nibbles at a
 * time. A next-index table would save one compare per nibble for 712 more bytes;
 * it is left out because the SH-2's 4 KB cache and cartridge space are the
 * scarcer resources on the targets this was written for. */
#ifndef SH2ZAM_ADPCM_H
#define SH2ZAM_ADPCM_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SH2Z_IMA_ROWS 89

typedef struct {
    int32_t pred;  /**< last decoded sample, -32768..32767 */
    int32_t index; /**< step index, 0..88 */
} sh2z_ima_state_t;

/** The IMA step table (defined with the reference decoder, sh2zam_adpcm_ref.c). */
extern const uint16_t sh2z_ima_step[SH2Z_IMA_ROWS];
/** Precomputed delta for (index, nibble & 7): row-major, 8 per row. */
extern const uint16_t sh2z_ima_diff[SH2Z_IMA_ROWS * 8];

/** Decode `count` nibbles starting at nibble position `pos` of `src` into out[0..count-1]. */
void sh2z_ima_decode(sh2z_ima_state_t *state, const uint8_t *src, uint32_t pos,
                     int16_t *out, size_t count);

/** Specification: the same contract, one nibble at a time with the bitwise rule. */
void sh2z_ima_ref_decode(sh2z_ima_state_t *state, const uint8_t *src, uint32_t pos,
                         int16_t *out, size_t count);

#ifdef __cplusplus
}
#endif
#endif
