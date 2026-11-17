# Rectangle-list bank ownership

The first actual compositor fixture exposed an existing clear-path mismatch.
Drawing appends rectangles using tracked bank `bkp`, while four clear statements
selected rectangle lists/counts using `go & 1`. A held pass can advance GO
without flipping banks, so those selectors can diverge.

The reference trace records rectangle count 22 at completed GO2503, then its
count becoming zero when GO2504 starts drawing the other tracked bank. The
original proof correctly failed its completed-metadata gate. Its inputs,
captures and checker remain archived; no pixel or performance pass is claimed.

The repair changes eight macro arguments across those four statements to
`bkp`, matching the drawing and span paths. It covers slave/master counts and
the legacy rectangle-clear list selection. Active ROWCLR uses row spans for
pixels, so this establishes corrected metadata ownership, not a visible sprite
fix. The tracked bank index still needs calibration against physical DRAM when
interpreting emulator dumps.

`python3 tests/diagnostics/check_rectangle_bank.py` passes 37 actual-source
sanitizer cases, including held flips, wraparound and preservation of the other
bank. Seven old-fragment negative controls expose count or pixel-selection
failures. The second private fixture pair includes this same correction in
both arms. The corrected wsbr03/wsbc03 pair passes the complete target proof;
the mistakenly built second pair was rejected before execution.

Normal wsplay09 passes all five level starts, three/six-button menu reentry,
65,001-frame health and the 40,000-frame natural route regression. Across
36,966 completed common clock keys, all 440 mirror bytes and 18 fields match;
all 138 coherent ordered boss/level states match. Raw/in-flight differences are
retained. Assets, game code and MD payload match prior08 exactly; only 484
changed bytes inside S_main and the recognized build stamp differ. The separate
foreground-view binding hold remains open, so this is not a new accepted alpha.

The normal09 office capture has 24 sparse samples over 180 seconds. Sample 13
is nearly all black, followed by nonblack samples 14 through 24; this is not
continuous blackout, pixel-parity or cadence proof. The wrapper selected
accepted rdplay01 again at 19:44:01, and remote hash/path verification passed.

Evidence: `build/codex-sprite-block-audit/target-compositor-fixture/actual-target-proof/`
and `target-compositor-fixture02/`. Neither probe is an accepted player ROM.
