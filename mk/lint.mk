# Lint support: print the define sets without building; `make lint`.
# Part of the top-level Makefile; included, never run on its own.
# ---- LINT SUPPORT (2026-09-17) ----
# Print the EXACT -D sets a build compiles with, WITHOUT building.
# `make -n` is not usable for this (LESSONS "make -n REWRITES
# .build_flags"), and neither is this: the FLAGSTAMP `$(shell ...)` above
# runs at PARSE time on every invocation, so any make rewrites the stamp.
# tools/lint.sh saves and restores it around these targets. The recursion
# mirrors `line:` -> `ship-us:` exactly so the two cannot drift.
.PHONY: print-defs line-defs shipus-defs lint lint-generic lint-32x
print-defs:
	@echo "MD $(MDCCFLAGS)"
	@echo "SH $(SHCCFLAGS)"
shipus-defs:
	@$(MAKE) --no-print-directory GAME=$(GAME) $(SHIP_US) print-defs
line-defs:
	@$(MAKE) --no-print-directory $(LINE_FLAGS) shipus-defs
# THE SHIPPING SET since the port (2026-10-29): `make candidate` = line + PORT_FLAGS +
# CANDIDATE_FLAGS. line-defs alone is the September 68K-shim line, which nothing ships.
candidate-defs:
	@$(MAKE) --no-print-directory $(LINE_FLAGS) $(PORT_FLAGS) $(CANDIDATE_FLAGS) shipus-defs

# `make lint` = the whole health gate. See tools/lint.sh.
lint:
	@tools/lint.sh
lint-generic:
	@tools/lint.sh --generic
lint-32x:
	@tools/lint.sh --32x

# The blob's cart address lives in TWO files: the AT() in sh_src/mars.ld
# and the 68K's recompute in md_src/md_main.c. A silent mismatch makes
# the 68K fetch gap fill and render garbage a long way from its cause,
# which is exactly how .tilesmd's missing "a" flag cost an evening.
#
# AT THE END OF THE FILE ON PURPOSE. Defined up among the flag blocks it
# became the FIRST target in the Makefile and therefore the DEFAULT
# GOAL, so a plain `make` ran the check and built no rom at all -- which
# is how it broke `make line` the moment it was added.
.PHONY: tilesmd-addr
tilesmd-addr:
	@python3 tools/check_tilesmd_addr.py
