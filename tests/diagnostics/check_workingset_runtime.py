#!/usr/bin/env python3
"""Actual generic raw lookup + isolated game policy under host sanitizers."""
from pathlib import Path
import os,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='workset-runtime-') as tmp:
    exe=Path(tmp)/'check'
    subprocess.run([os.environ.get('CC','clang'),'-std=c99','-O2','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root/'sh_src/r60'),str(Path(__file__).with_suffix('.c')),'-o',str(exe)],check=True)
    subprocess.run([str(exe),*sys.argv[1:]],check=True)

    protocol=Path(tmp)/'protocol'
    subprocess.run([os.environ.get('CC','clang'),'-std=c99','-O2','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(Path(__file__).with_name('check_workingset_protocol.c')),'-o',str(protocol)],check=True)
    subprocess.run([str(protocol)],check=True)

    schedule=Path(tmp)/'schedule'
    subprocess.run([os.environ.get('CC','clang'),'-std=c99','-O2','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(Path(__file__).with_name('check_workingset_schedule.c')),'-o',str(schedule)],check=True)
    subprocess.run([str(schedule)],check=True)

    admission=Path(tmp)/'admission'
    subprocess.run([os.environ.get('CC','clang'),'-std=c99','-O2','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(Path(__file__).with_name('check_workingset_admission.c')),'-o',str(admission)],check=True)
    subprocess.run([str(admission),*sys.argv[1:]],check=True)
