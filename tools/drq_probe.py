#!/usr/bin/env python3
"""LOOP 17 — decode the DRQPROBE histogram from an ares savestate.

    tools/drq_probe.py rom/s16.bs9

THE QUESTION: sprite-push truncation is worth ~18 lines/vint of 68K time
(measured), and all of it rests on the master being able to read a
PARTIAL DREQ landing out of TCR0. MAME cannot answer that (LOOP 13:
"MAME never showed it"), so the build pushes one record short on every
16th sprite packet and counts what `landed` came back as.

    [2] tracking ~1/16 of [0]  -> PARTIAL LANDINGS ARE READABLE.
        Truncation lands as written; go implement it.
    [2] zero, [3] carrying them -> NOT readable.
        Use exact-match arming instead (LOOP17 next-attempt b): the MD
        publishes the next push's record count through COMM and the
        master arms exactly that, so TE sets and nothing depends on a
        partial read.
"""
import struct
import sys


def swap16(b):
    b = bytearray(b)
    for i in range(0, len(b) - 1, 2):
        b[i], b[i + 1] = b[i + 1], b[i]
    return bytes(b)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "rom/s16.bs9"
    st = open(path, "rb").read()
    sd = 0x23B

    def rd32(off):
        return struct.unpack(">I", swap16(st[sd + off:sd + off + 4]))[0]

    print(f"STATE: {path}")
    print(f"BUILD: {rd32(0x28000 + 18 * 4):08x}")
    d = [rd32(0x28FBC + i * 4) for i in range(9)]
    names = ["sprite windows", "  landed==596 (complete)",
             "  landed==588 (SHORT PUSH, PARTIAL READ)",
             "  landed==0   (nothing readable)",
             "  landed==other", "  last other value",
             "text landed==852", "text landed==596 (partial, no-pal)",
             "text other/0"]
    for n, v in zip(names, d):
        print(f"{v:8d}  {n}")

    tot, short, zero = d[0], d[2], d[3]
    if tot == 0:
        print("\nNO DATA — is this a DRQPROBE=1 build? "
              "(tools/build_id.py show should say DRQPROBE)")
        return
    exp = tot / 16.0
    print(f"\nexpected short pushes ~{exp:.0f} (1 in 16 of {tot})")
    if short >= exp * 0.5:
        print("VERDICT: PARTIAL LANDINGS ARE READABLE. The short pushes "
              "came back at their true length, so sprite-push truncation "
              "works as written — implement it.")
    elif zero >= exp * 0.5:
        print("VERDICT: PARTIAL LANDINGS ARE NOT READABLE. Short pushes "
              "read as 0, so the master cannot see them. Use exact-match "
              "arming (MD publishes the next nrec via COMM, master arms "
              "it) so TE sets on every packet.")
    else:
        print("VERDICT: INCONCLUSIVE — neither bucket took the short "
              "pushes. Check [4]/[5] for what they actually read.")


if __name__ == "__main__":
    main()
