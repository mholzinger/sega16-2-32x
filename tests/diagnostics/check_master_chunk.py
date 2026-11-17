import sys
#!/usr/bin/env python3
"""Actual optional ROM adapter under ASan/UBSan, active and idle control.

Only its arena addresses and host section attribute are overridden. The
adapter source is included directly; no source rewriting or ROM build.
"""
import os
from pathlib import Path
import subprocess
import tempfile

source=Path(__file__).with_suffix(".c")
root=source.resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="master-chunk-") as tmp:
    # Exercise the actual Make option block in isolation: no shared Makefile,
    # asset generation, object/ROM recipes or port shadow are invoked.
    sys.path.insert(0, str(root/"tools")); from mktext import makefile_text
    make_text=makefile_text(str(root))
    begin=make_text.index("# R60MCLEAD=")
    end=make_text.index("# `R60ZOOMWORDS=1`",begin)
    gate=Path(tmp)/"gates.mk"
    gate.write_text(make_text[begin:end]+"\n$(info FLAGS=$(SHCCFLAGS))\n.PHONY: show\nshow:\n\t@:\n")
    required="R60SPRPREP R60BAKEART R60PLOT5 R60ZOOMFAST R60ROWFAST R60SLAVEWIN R60KEEPSPR R60PALMAP".split()
    base={name:"1" for name in required}|{"R60MASTERCHUNK":"1","CARTSSF":"1"}
    gates=[({"R60MCLEAD":"8"},False,"requires R60MASTERCHUNK"),
           (base,True,"-DR60_MC_LOOKAHEAD=32"),
           (base|{"R60MCLEAD":"8"},True,"-DR60_MC_LOOKAHEAD=8"),
           (base|{"R60MCLEAD":"32"},True,"-DR60_MC_LOOKAHEAD=32"),
           (base|{"R60MCLEAD":"16"},True,"-DR60_MC_LOOKAHEAD=16"),
           (base|{"R60MCLEAD":"24"},False,"must be 8, 16 or 32"),
           (base|{"R60MCLEAD":"8 32"},False,"must be 8, 16 or 32"),
           ({"R60MCQUANTA":"8"},False,"requires R60MASTERCHUNK"),
           (base|{"R60MCQUANTA":"2"},False,"must be 1 or 8"),
           (base|{"R60MCQUANTA":"8"},True,"-DR60_MC_QUANTA=8"),({},True,""),(base,True,"-DR60_MC_STORESTATS=1"),
           (base|{"R60MCSTORESTATS":"0"},True,"-DR60_MC_STORESTATS=0"),
           (base|{"R60MASTERCHUNK":"2","R60MCSTORESTATS":"0"},True,"-DR60_MASTERCHUNK=2"),
           ({"R60MCSTORESTATS":"0"},False,"requires R60MASTERCHUNK"),
           (base|{"R60MCSTORESTATS":"2"},False,"must be 0 or 1"),
           (base|{"R60MCSTORESTATS":"0","R60BKBENCH":"1"},False,"incompatible with R60BKBENCH"),
           (base|{"R60MCSTORESTATS":"1","R60BKBENCH":"1"},True,"-DR60_MC_STORESTATS=1")]
    for values,valid,marker in gates:
        result=subprocess.run(["make","-s","-f",str(gate),"show",*(f"{k}={v}" for k,v in values.items())],text=True,capture_output=True)
        assert (result.returncode==0)==valid,(values,result.stdout,result.stderr)
        assert marker in result.stdout+result.stderr,(values,result.stdout,result.stderr)
    for stats,extra,marker in [(2,[],"R60_MC_STORESTATS must"),
                               (0,["-DR60_BKBENCH=1"],"R60BKBENCH requires")]:
        result=subprocess.run([os.environ.get("CC","clang"),"-std=c99","-fsyntax-only",
                               "-DR60_MASTERCHUNK=1",f"-DR60_MC_STORESTATS={stats}",
                               *extra,str(source)],text=True,capture_output=True)
        assert result.returncode and marker in result.stderr,result.stderr
    for quanta in (0,2,9):
        result=subprocess.run([os.environ.get("CC","clang"),"-std=c99","-fsyntax-only",
                "-DR60_MASTERCHUNK=1",f"-DR60_MC_QUANTA={quanta}",str(source)],text=True,capture_output=True)
        assert result.returncode and "R60_MC_QUANTA must" in result.stderr,result.stderr
    for stats in (0,1):
        expanded=subprocess.check_output([os.environ.get("CC","clang"),"-std=c99","-E","-P",
                "-DR60_MASTERCHUNK=1",f"-DR60_MC_STORESTATS={stats}",str(source)],text=True)
        assert ("r60_mc_row_pixels(" in expanded)==bool(stats),"packet rescan availability drift"
    print("PASS store-stat Make/source guards: defaults, MASTER-only, numeric0/1, BKBENCH rejection")
    for mode in (1,2):
        for lead in (8,32):
            for stats in (0,1):
                for quanta in (1,8):
                    exe=Path(tmp)/f"mode{mode}-lead{lead}-stats{stats}-quanta{quanta}"
                    subprocess.run([os.environ.get("CC","clang"),"-std=c99","-O2","-g",
                                    "-Wall","-Wextra","-fsanitize=address,undefined",
                                    "-fno-sanitize-recover=all","-DCART_SSF",
                                    f"-DR60_MASTERCHUNK={mode}",f"-DR60_MC_LOOKAHEAD={lead}",
                                    f"-DR60_MC_STORESTATS={stats}",f"-DR60_MC_QUANTA={quanta}",str(source),"-o",str(exe)],check=True)
                    subprocess.run([str(exe)],check=True)
