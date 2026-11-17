#!/usr/bin/env python3
"""Render our YM2612 stream (the Z80 player's format) through jt12 under Verilator. KIT.

    from jt12_run import jt12_render
    x = jt12_render(stream_bytes, seconds)      -> float64 samples at 53267 Hz (MD clock/144)

Builds build/jt12_ref from srcref/jtcores/modules/jt12 (git show) on first use, like
tools/opm_ab.py does for jt51. jt12 is the YM2612 the MiSTer Mega Drive core runs: the
hardware-truth proxy for the cart's FM lane, beside ares (tools/opm_stage2.sh).
"""
import os
import struct
import subprocess

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JT12 = os.path.join(ROOT, 'srcref', 'jtcores', 'modules', 'jt12')
MD_CLOCK = 7670454
RATE = MD_CLOCK / 144.0


def build():
    b = os.path.join(ROOT, 'build', 'jt12_ref')
    exe = os.path.join(b, 'obj_dir', 'jt12_ref')
    src = os.path.join(ROOT, 'tools', 'jt12_ref.cpp')
    if os.path.exists(exe) and os.path.getmtime(exe) > os.path.getmtime(src):
        return exe
    rtl = os.path.join(b, 'rtl')
    os.makedirs(rtl, exist_ok=True)
    names = subprocess.run(['git', '-C', JT12, 'ls-tree', '-r', '--name-only', 'HEAD', 'hdl/'],
                           capture_output=True, text=True, check=True).stdout.split()
    for n in names:
        parts = n.split('/')
        if (len(parts) == 2 or parts[1] in ('dac', 'adpcm')) and n.endswith(('.v', '.vh')):
            body = subprocess.run(['git', '-C', JT12, 'show', 'HEAD:' + n], capture_output=True, check=True).stdout
            open(os.path.join(rtl, parts[-1]), 'wb').write(body)
    subprocess.run(['verilator', '--cc', '--exe', '--build', '-O3', '-Wno-fatal', '-Wno-lint', '-Wno-style',
                    '--top-module', 'jt12', '-Irtl', 'rtl/jt12.v', src, '-o', 'jt12_ref', '-j', '8'],
                   cwd=b, check=True, capture_output=True)
    return exe


def events(stream):
    """port-level events <sample, port, val> from the Z80 stream: delays < 0x80 are ms,
    0xF0/0xF1 reg val are bank 0/1 writes, 0xF2 is PSG (dropped), 0xFF ends."""
    out = []
    t_ms = 0.0
    i = 0
    while i < len(stream):
        b = stream[i]
        if b < 0x80:
            t_ms += b
            i += 1
        elif b in (0xF0, 0xF1):
            samp = int(round(t_ms * RATE / 1000.0))
            port = 2 * (b & 1)
            out.append((samp, port, stream[i + 1]))
            out.append((samp, port + 1, stream[i + 2]))
            i += 3
        elif b == 0xF2:
            i += 2
        elif b == 0xFF:
            break
        else:
            i += 1
    return out


def jt12_render(stream, seconds, workdir=None):
    workdir = workdir or os.path.join(ROOT, 'build', 'jt12_ref', 'run')
    os.makedirs(workdir, exist_ok=True)
    evp = os.path.join(workdir, 'events.bin')
    with open(evp, 'wb') as f:
        for samp, port, val in events(stream):
            f.write(struct.pack('<IBB', samp, port, val))
    outp = os.path.join(workdir, 'out.s16')
    n = int(seconds * RATE)
    subprocess.run([build(), evp, outp, str(n)], check=True, capture_output=True)
    return np.fromfile(outp, dtype='<i2').astype(np.float64)
