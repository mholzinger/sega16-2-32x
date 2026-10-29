#!/usr/bin/env python3
"""The scene map of an arcade oracle dump (2026-09-25).

    tools/oracle_scenes.py oracle [--csv out.csv]

Reads oracle/state.csv (tools/inp_dump.lua) and prints, in GAME frames, every
moment worth aligning our rom against:

  round N starts        the round byte 0xFFF142 changes
  cut pages             the FG page word becomes 0xAAAA (the transformation and
                        the between-round screens draw from pages 10/11)
  solo filter           0xFFF148 non-zero -- the game runs ONE object's routine
                        and diverts every other slot to 0x3F04 (the cutscene's
                        own switch; NOTES-FROM-DECOMPILE 17)
  display off           the game blanks its own display (0xFFF026 bit 0)
  page changes          any other change of the FG/BG page words = a scene load

Game frames are what tools/gf_align.py takes, so a line here is a command:

    tools/gf_align.py rom/s16.32x oracle <gf> <gf> ... --tag x
"""
import argparse, csv, os, sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("oracle")
    ap.add_argument("--csv", default=None)
    a = ap.parse_args()

    rows = list(csv.DictReader(open(os.path.join(a.oracle, "state.csv"))))
    if not rows:
        raise SystemExit("state.csv is empty")

    events = []
    prev = None
    for r in rows:
        gf = int(r["gf"])
        cur = dict(round=r["round"], p0=r["page0"], p1=r["page1"],
                   solo=(r["solo"] != "0"), disp=(int(r["disp"]) & 1) == 0)
        if prev is None:
            events.append((gf, "start", f"round {cur['round']} pages {cur['p0']}/{cur['p1']}"))
        else:
            if cur["round"] != prev["round"]:
                events.append((gf, "round", f"{prev['round']} -> {cur['round']}"))
            if cur["p0"] != prev["p0"] or cur["p1"] != prev["p1"]:
                what = "CUT PAGES" if cur["p0"] == "AAAA" else (
                       "leaves cut pages" if prev["p0"] == "AAAA" else "pages")
                events.append((gf, what, f"{prev['p0']}/{prev['p1']} -> {cur['p0']}/{cur['p1']}"))
            if cur["solo"] != prev["solo"]:
                events.append((gf, "solo", "on (cutscene)" if cur["solo"] else "off"))
            if cur["disp"] != prev["disp"]:
                events.append((gf, "display", "OFF (blanked)" if cur["disp"] else "on"))
        prev = cur

    last = int(rows[-1]["gf"])
    print(f"{len(rows)} frames, {last} game frames, {len(events)} scene events\n")
    print(f"{'game frame':>10}  {'event':<18} detail")
    for gf, what, detail in events:
        print(f"{gf:10d}  {what:<18} {detail}")

    if a.csv:
        with open(a.csv, "w", newline="") as f:
            w = csv.writer(f); w.writerow(["gf", "event", "detail"]); w.writerows(events)
        print(f"\nwrote {a.csv}")

    # a short list of the moments Mike's seven defects live in
    cuts = [gf for gf, what, _ in events if what == "CUT PAGES"]
    rounds = [(gf, d) for gf, what, d in events if what == "round"]
    print("\nsuggested targets")
    if cuts:
        print("  cut screens (transformation / between rounds):",
              " ".join(str(c) for c in cuts[:24]))
    if rounds:
        print("  round changes:", " ".join(f"{gf}({d})" for gf, d in rounds[:24]))


if __name__ == "__main__":
    main()
