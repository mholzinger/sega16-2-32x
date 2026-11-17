#!/usr/bin/env python3
"""Build and sanitize the target-portable MC1 decoder/admission helper."""
import csv
import json
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    compiler = os.environ.get('CC', 'clang')
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / 'check_sprite_macro'
        subprocess.run([
            compiler, '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-I', str(ROOT / 'sh_src/r60'),
            str(ROOT / 'tests/diagnostics/check_sprite_macro.c'), '-o', str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True)
        current = ROOT/'build/codex-sound-compact/smoke-owner-macros-port-v4.mc1'
        # Older OP1 artifacts intentionally remain available as comparison
        # fixtures, but the v4 decoder must not be asked to accept them.
        if current.is_file():
            subprocess.run([str(exe), str(current)], check=True)
            report = current.with_suffix('.json')
            corpus = ROOT/'build/codex-sound-compact/port-smoke-corpus/sprite-watch.csv'
            if report.is_file() and corpus.is_file():
                tick = json.loads(report.read_text())['selected'][0]['first_tick']
                with corpus.open(newline='') as source:
                    row = next(item for item in csv.DictReader(source)
                               if int(item['frame']) == tick)
                capture = Path(tmp) / 'selected-sprite-table.bin'
                capture.write_bytes(bytes.fromhex(row['sdram:0x28400:0x400']))
                subprocess.run([str(exe), str(current), str(capture)], check=True)
        shared = ROOT/'build/codex-sound-compact/smoke-owner-macros-shared-v7.mc1'
        shared_report = shared.with_suffix('.json')
        corpus = ROOT/'build/codex-sound-compact/port-smoke-corpus/sprite-watch.csv'
        if shared.is_file() and shared_report.is_file() and corpus.is_file():
            subprocess.run([str(exe), str(shared)], check=True)
            tick = json.loads(shared_report.read_text())['selected'][0]['first_tick']
            with corpus.open(newline='') as source:
                row = next(item for item in csv.DictReader(source)
                           if int(item['frame']) == tick)
            capture = Path(tmp) / 'shared-sprite-table.bin'
            capture.write_bytes(bytes.fromhex(row['sdram:0x28400:0x400']))
            subprocess.run([str(exe), str(shared), str(capture)], check=True)
    print('sprite macro decoder: PASS')


if __name__ == '__main__':
    main()
