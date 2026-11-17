# The flag stamp and the generated linker scripts. Included last so the stamp sees every -D.
# Part of the top-level Makefile; included, never run on its own.
# ---- THE FLAG STAMP, LAST IN THE FILE SO IT SEES EVERY -D ----------------------
# Must stay the final parse-time statement: anything that appends to MDCCFLAGS or
# SHCCFLAGS below this point is invisible to the stamp and therefore invisible to
# make. See the note at the FLAGSTAMP definition for what that cost.
ifndef FLAGSTAMP_SKIP
$(shell f='$(MDCCFLAGS) $(SHCCFLAGS)'; \
        [ "$$(cat $(FLAGSTAMP) 2>/dev/null)" = "$$f" ] || printf '%s' "$$f" > $(FLAGSTAMP))
$(shell f='$(CPORT_ROOTS_SET) | $(CPORT_STUB_WAIT) | $(CPORT_OPT_SET) | $(CPORT_CLAMP) | $(CPORT_A6) | $(CPORT_HOT) | $(CPORT_AFTER) | $(CPORT_ENTRY_GUARDS)'; \
        [ "$$(cat $(PORTSTAMP) 2>/dev/null)" = "$$f" ] || printf '%s' "$$f" > $(PORTSTAMP))
endif

# the 4 MB and 6 MB linker scripts (CARTMB, see SHLDFLAGS); kept at the end so they are never the default goal
sh_src/mars6.ld: sh_src/mars.ld
	sed -e 's/LENGTH = 0x00800000/LENGTH = 0x00600000/' \
	    -e 's/ASSERT(0x0244D000 + SIZEOF(\.portcode) <= 0x02800000/ASSERT(0x0244D000 + SIZEOF(.portcode) <= 0x02600000/' $< > $@
sh_src/mars4.ld: sh_src/mars.ld
	sed -e 's/LENGTH = 0x00800000/LENGTH = 0x00400000/' \
	    -e 's/\.sfxdata 0x022FE400 : AT(0x002FE400)/.sfxdata 0x02292000 : AT(0x00292000)/' \
	    -e 's/SIZEOF(\.sfxdata) <= 0x02400000, ".sfxdata overflows past the low 4 MB (the 68K-visible region)"/SIZEOF(.sfxdata) <= 0x022F8000, ".sfxdata overflows into .palscenes at 0x2F8000"/' \
	    -e 's/ASSERT(ADDR(\.sounddata) + SIZEOF(\.sounddata) <= 0x022F8000/ASSERT(ADDR(.sounddata) + SIZEOF(.sounddata) <= 0x02292000/' \
	    -e 's/\.portrom 0x02400000 : AT(0x00400000)/.portrom 0x02300000 : AT(0x00300000)/' \
	    -e 's/\.portdata 0x02440000 : AT(0x00440000)/.portdata 0x02340000 : AT(0x00340000)/' \
	    -e 's/\.portcode 0x0244D000 : AT(0x0044D000)/.portcode 0x0234D000 : AT(0x0034D000)/' \
	    -e 's/ASSERT(0x0244D000 + SIZEOF(\.portcode) <= 0x02800000/ASSERT(0x0234D000 + SIZEOF(.portcode) <= 0x02400000/' $< > $@
# CARTSSF=1: the 4 MB layout with the rom region opened to 16 MB and the paged region
# (.pages) placed at 0x400000. Nothing addressed straight moves; the 68K-visible bound holds.
# THE RULE IS tools/gen_ld_ssf.py (2026-11-02). db2404e4 added it, 37273e6f reverted it, 7873fcad
# brought the repacked layout back WITHOUT the rule: every build since linked against a stale
# generated mars4s.ld in the shadow (the inline python below it only widened the rom and added
# .pages). Deleting that file reproduced the old layout. The generator is the one source.
sh_src/mars4s.ld: sh_src/mars4.ld tools/gen_ld_ssf.py $(FLAGSTAMP)
	python3 tools/gen_ld_ssf.py $< $@ $(R60_CODE_LD_ARGS) $(if $(LAUNCHER),--launcher)
