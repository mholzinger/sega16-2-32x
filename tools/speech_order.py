#!/usr/bin/env python3
"""Put the command-sweep utterances in the order the shipped speech bank holds them.

    python3 tools/speech_order.py sndtest/speech/manifest.json

tools/soundmap_build.py appends the sweep's new utterances in command order. The bank the
port was tested on was extended in two passes (the five short ones last), and the bank
order is the blob layout, so one pass would ship a different .sounddata than was tested.
Same samples either way; this pins the layout. Attract-tap entries are untouched.
"""
import json
import sys

ORDER = ['0x42', '0x44', '0x47', '0x48', '0x4A', '0x4D', '0x4E', '0x52', '0x53', '0x54',
         '0x55', '0x56', '0x45', '0x49', '0x4B', '0x4C', '0x50']
path = sys.argv[1]
m = json.load(open(path))
head = [e for e in m if not e.get('wav', '').startswith('cmd_')]
sweep = sorted((e for e in m if e.get('wav', '').startswith('cmd_')),
               key=lambda e: ORDER.index(e['cmds_before'][0]) if e['cmds_before'][0] in ORDER else len(ORDER))
for i, e in enumerate(head + sweep):
    e['index'] = i
with open(path, 'w') as f:
    json.dump(head + sweep, f, indent=1)
