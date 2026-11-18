# SH2ZAM (vendored subset)

From https://github.com/lamb2k/SH2ZAM (MIT, see LICENSE), module `adpcm-1`
(commit 4c28e97, lamb2k/SH2ZAM PR 24): exact IMA/DVI ADPCM decoding for the SH-2.

| Here | Upstream |
|---|---|
| `adpcm.h` | `saturn/include/sh2zam/adpcm.h` |
| `sh2zam_adpcm.c` | `saturn/src/sh2zam_adpcm.c` (include path adjusted: `"adpcm.h"`) |
| `sh2zam_adpcm_tables.h` | `saturn/src/sh2zam_adpcm_tables.h` |

Only the fast path is vendored. The reference decoder, step table, tests and the SH-2
model benchmark stay upstream (`saturn/ADPCM.md`). Change it upstream and copy it back.
Used by the speech lane, `sh_src/r60/r60_speech.inc`, when `R60SPEECH=1`.
