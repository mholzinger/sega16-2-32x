#!/usr/bin/env python3
"""Generate sndtest/opm/opm_tables.h, the YM2151 constant tables for sndtest/opm/opm.c. KIT.

    python3 tools/opm_tables.py            # write the header
    python3 tools/opm_tables.py --check    # exit 1 if the header is stale
    python3 tools/opm_tables.py --ymfm-diff [~/src/ymfm/src/ymfm_fm.ipp]

Every table is computed from its formula, not copied (2026-10-30, hybrid 1):

  SIN     quarter-wave log-sine, 4.8 attenuation: round(-log2(sin((2i+1)pi/1024)) * 256)
  POW     2^-x mantissas, 10 bits + implied 0x400: round((2^(j/256) - 1) * 1024), stored
          reversed and pre-shifted << 2 so the lookup is one shift (ymfm's layout)
  STEP    phase step (0.10 phase units per FM sample) for the 768 key-code fractions of
          block 7: the chip's own values as measured from silicon (ymfm, BSD; see
          STEP_MEASURED), because the chip interpolates KF in a 32/64 pattern no formula
          reproduces -- the formula is kept as step_table_formula() for the diff

DT1 and the envelope steps are computed in opm.c the way jt51 computes them (jt51_pg.v,
jt51_eg.v), so they need no table here.

--ymfm-diff checks each table against ymfm's (SIN and POW are formula-derived and identical;
STEP is the same measured data and must be identical) and reports how far the step formula
is from it (630 of 768 entries, at most 2.72 cents, measured 2026-10-30). jt51_phinc_rom.v
holds the same measured values / 32; tools/opm_ab.py --rom-steps runs the model on those.
"""
import argparse
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'sndtest', 'opm', 'opm_tables.h')
FM_RATE = 3579545.0 / 64.0


def sin_table():
    return [round(-math.log2(math.sin((2 * i + 1) * math.pi / 1024.0)) * 256.0) for i in range(256)]


def pow_table():
    m = [round((2.0 ** (j / 256.0) - 1.0) * 1024.0) for j in range(256)]
    return [((m[255 - i]) | 0x400) << 2 for i in range(256)]


# The chip's phase steps for the 768 key-code fractions of block 7, as measured from
# silicon: from ymfm (Aaron Giles, BSD-3-Clause, src/ymfm_fm.ipp s_phase_step), itself
# David Viens' analysis. Mike chose this over the formula on 2026-10-31: the formula is
# within 2.7 cents but the chip interpolates KF in an irregular 32/64 pattern no formula
# reproduces, and the SH-2 synth is gated sample-for-sample against the arcade RTL.
#
#   Copyright (c) 2021, Aaron Giles. All rights reserved. Redistribution of this table
#   with the above notice is permitted under the BSD 3-Clause License (~/src/ymfm/LICENSE).
STEP_MEASURED = [
    41568,41600,41632,41664,41696,41728,41760,41792,41856,41888,41920,41952,42016,42048,42080,42112,
    42176,42208,42240,42272,42304,42336,42368,42400,42464,42496,42528,42560,42624,42656,42688,42720,
    42784,42816,42848,42880,42912,42944,42976,43008,43072,43104,43136,43168,43232,43264,43296,43328,
    43392,43424,43456,43488,43552,43584,43616,43648,43712,43744,43776,43808,43872,43904,43936,43968,
    44032,44064,44096,44128,44192,44224,44256,44288,44352,44384,44416,44448,44512,44544,44576,44608,
    44672,44704,44736,44768,44832,44864,44896,44928,44992,45024,45056,45088,45152,45184,45216,45248,
    45312,45344,45376,45408,45472,45504,45536,45568,45632,45664,45728,45760,45792,45824,45888,45920,
    45984,46016,46048,46080,46144,46176,46208,46240,46304,46336,46368,46400,46464,46496,46528,46560,
    46656,46688,46720,46752,46816,46848,46880,46912,46976,47008,47072,47104,47136,47168,47232,47264,
    47328,47360,47392,47424,47488,47520,47552,47584,47648,47680,47744,47776,47808,47840,47904,47936,
    48032,48064,48096,48128,48192,48224,48288,48320,48384,48416,48448,48480,48544,48576,48640,48672,
    48736,48768,48800,48832,48896,48928,48992,49024,49088,49120,49152,49184,49248,49280,49344,49376,
    49440,49472,49504,49536,49600,49632,49696,49728,49792,49824,49856,49888,49952,49984,50048,50080,
    50144,50176,50208,50240,50304,50336,50400,50432,50496,50528,50560,50592,50656,50688,50752,50784,
    50880,50912,50944,50976,51040,51072,51136,51168,51232,51264,51328,51360,51424,51456,51488,51520,
    51616,51648,51680,51712,51776,51808,51872,51904,51968,52000,52064,52096,52160,52192,52224,52256,
    52384,52416,52448,52480,52544,52576,52640,52672,52736,52768,52832,52864,52928,52960,52992,53024,
    53120,53152,53216,53248,53312,53344,53408,53440,53504,53536,53600,53632,53696,53728,53792,53824,
    53920,53952,54016,54048,54112,54144,54208,54240,54304,54336,54400,54432,54496,54528,54592,54624,
    54688,54720,54784,54816,54880,54912,54976,55008,55072,55104,55168,55200,55264,55296,55360,55392,
    55488,55520,55584,55616,55680,55712,55776,55808,55872,55936,55968,56032,56064,56128,56160,56224,
    56288,56320,56384,56416,56480,56512,56576,56608,56672,56736,56768,56832,56864,56928,56960,57024,
    57120,57152,57216,57248,57312,57376,57408,57472,57536,57568,57632,57664,57728,57792,57824,57888,
    57952,57984,58048,58080,58144,58208,58240,58304,58368,58400,58464,58496,58560,58624,58656,58720,
    58784,58816,58880,58912,58976,59040,59072,59136,59200,59232,59296,59328,59392,59456,59488,59552,
    59648,59680,59744,59776,59840,59904,59936,60000,60064,60128,60160,60224,60288,60320,60384,60416,
    60512,60544,60608,60640,60704,60768,60800,60864,60928,60992,61024,61088,61152,61184,61248,61280,
    61376,61408,61472,61536,61600,61632,61696,61760,61824,61856,61920,61984,62048,62080,62144,62208,
    62272,62304,62368,62432,62496,62528,62592,62656,62720,62752,62816,62880,62944,62976,63040,63104,
    63200,63232,63296,63360,63424,63456,63520,63584,63648,63680,63744,63808,63872,63904,63968,64032,
    64096,64128,64192,64256,64320,64352,64416,64480,64544,64608,64672,64704,64768,64832,64896,64928,
    65024,65056,65120,65184,65248,65312,65376,65408,65504,65536,65600,65664,65728,65792,65856,65888,
    65984,66016,66080,66144,66208,66272,66336,66368,66464,66496,66560,66624,66688,66752,66816,66848,
    66944,66976,67040,67104,67168,67232,67296,67328,67424,67456,67520,67584,67648,67712,67776,67808,
    67904,67936,68000,68064,68128,68192,68256,68288,68384,68448,68512,68544,68640,68672,68736,68800,
    68896,68928,68992,69056,69120,69184,69248,69280,69376,69440,69504,69536,69632,69664,69728,69792,
    69920,69952,70016,70080,70144,70208,70272,70304,70400,70464,70528,70560,70656,70688,70752,70816,
    70912,70976,71040,71104,71136,71232,71264,71360,71424,71488,71552,71616,71648,71744,71776,71872,
    71968,72032,72096,72160,72192,72288,72320,72416,72480,72544,72608,72672,72704,72800,72832,72928,
    72992,73056,73120,73184,73216,73312,73344,73440,73504,73568,73632,73696,73728,73824,73856,73952,
    74080,74144,74208,74272,74304,74400,74432,74528,74592,74656,74720,74784,74816,74912,74944,75040,
    75136,75200,75264,75328,75360,75456,75488,75584,75648,75712,75776,75840,75872,75968,76000,76096,
    76224,76288,76352,76416,76448,76544,76576,76672,76736,76800,76864,76928,77024,77120,77152,77248,
    77344,77408,77472,77536,77568,77664,77696,77792,77856,77920,77984,78048,78144,78240,78272,78368,
    78464,78528,78592,78656,78688,78784,78816,78912,78976,79040,79104,79168,79264,79360,79392,79488,
    79616,79680,79744,79808,79840,79936,79968,80064,80128,80192,80256,80320,80416,80512,80544,80640,
    80768,80832,80896,80960,80992,81088,81120,81216,81280,81344,81408,81472,81568,81664,81696,81792,
    81952,82016,82080,82144,82176,82272,82304,82400,82464,82528,82592,82656,82752,82848,82880,82976,
]


def step_table():
    return list(STEP_MEASURED)


def step_table_formula():
    """what a formula gives: equal temperament, A = 3520 Hz at index 512, rounded to the
    chip's step granularity of 32. Kept for --ymfm-diff to show how far it is."""
    out = []
    for i in range(768):
        f = 3520.0 * 2.0 ** ((i - 512) / 768.0)
        out.append(int(round(f * (1 << 20) / FM_RATE / 32.0)) * 32)
    return out



def emit():
    sin, pw, st = sin_table(), pow_table(), step_table()
    L = ['/* AUTO-GENERATED by tools/opm_tables.py -- do not edit. YM2151 constants from their',
         ' * formulas; see the generator for each one and --ymfm-diff for the check. */',
         '#ifndef OPM_TABLES_H', '#define OPM_TABLES_H', '#include <stdint.h>', '']

    def arr(ctype, name, vals, per, fmt):
        L.append('static const %s %s[%d] = {' % (ctype, name, len(vals)))
        for i in range(0, len(vals), per):
            L.append('\t' + ','.join(fmt % v for v in vals[i:i + per]) + ',')
        L.append('};')
        L.append('')

    arr('uint16_t', 'opm_sin', sin, 16, '0x%03x')
    arr('uint16_t', 'opm_pow', pw, 16, '0x%04x')
    arr('uint32_t', 'opm_step', st, 12, '%d')
    L.append('#endif')
    return '\n'.join(L) + '\n'


def ymfm_tables(path):
    src = open(os.path.expanduser(path)).read()

    def grab(name):
        m = re.search(name + r'(?:\[[^\]]*\])+\s*=\s*\{(.*?)\};', src, re.S)
        body = re.sub(r'//[^\n]*', '', m.group(1))
        return body

    sin = [int(x, 16) for x in re.findall(r'0x[0-9a-f]+', grab('s_sin_table'))]
    pw = [((int(x, 16) | 0x400) << 2) for x in re.findall(r'X\((0x[0-9a-f]+)\)', grab('s_power_table'))]
    st = [int(x) for x in re.findall(r'\d+', grab('s_phase_step'))]
    return {'sin': sin, 'pow': pw, 'step': st}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--ymfm-diff', nargs='?', const='~/src/ymfm/src/ymfm_fm.ipp')
    a = ap.parse_args()
    if a.ymfm_diff:
        y = ymfm_tables(a.ymfm_diff)
        ours = {'sin': sin_table(), 'pow': pow_table(), 'step': step_table()}
        bad = 0
        for k in ours:
            d = [i for i, (u, v) in enumerate(zip(ours[k], y[k])) if u != v]
            bad += len(d)
            extra = ''
            if k == 'step':
                f = step_table_formula()
                df = [i for i, (u, v) in enumerate(zip(f, y[k])) if u != v]
                cents = max(abs(1200 * math.log2(f[i] / y[k][i])) for i in df) if df else 0
                extra = '; the formula would differ in %d (worst %.2f cents)' % (len(df), cents)
            print('%-5s %4d entries, %3d differ%s%s' % (k, len(ours[k]), len(d), extra,
                                                     (': ' + ' '.join(map(str, d[:12]))) if d else ''))
        return 0
    text = emit()
    if a.check:
        if not os.path.exists(OUT) or open(OUT).read() != text:
            print('opm_tables.py: %s is stale' % os.path.relpath(OUT, ROOT))
            return 1
        return 0
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, 'w').write(text)
    print('wrote', os.path.relpath(OUT, ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
