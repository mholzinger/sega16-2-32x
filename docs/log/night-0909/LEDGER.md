# Night ledger 2026-09-09 (tools/night_run.py; roms in rom/night/)

Speed = game-frames per vint on the level-1 script, 100% = 60 fps. Windows are 700-vint slices of [1500,4100]. anim/black/colours/ramp are guards, not rankings. A gap under 6 points is noise (LOOP28 88).

| tag | flags | md5 | total% | 1500-2200 | 2200-2900 | 2900-3600 | 3600-4100 | miss% | by-miss windows | anim mean | black% f2000/3000/4000 | colours | ramp[3] | diff vs base | wall s | note |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| smoke_current |  | 88d9514c | 46.1 | 43.0 | 49.4 | 46.4 | 45.2 | 53.9 |  | 8.0 | 17.9/17.9/17.1 | 4116/1861/3574 | 0 |  | 191 | rig smoke test on whatever rom/s16.32x was (FBXSTAGE+FBXBOTH stamp, not the accepted rom) |
| base | `FBXPORT=1` | 1812b490 | 49.7 | 49.7 | 51.6 | 50.0 | 46.8 | 50.3 |  | 11.7 | 4.4/4.4/4.4 | 7610/6535/7743 | 1919 |  | 148 | A1 accepted line (mister-keeper-20260908 flags) |
| opt1 | `FBXPORT=1 TXTWRAM=1 LATESTEAL0=1 LATEKEEP=1 DRAWADOPT=1` | b436ea15 | 85.2 | 58.6 | 87.6 | 98.9 | 100.0 | 14.8 |  | 28.0 | 3.7/3.6/3.6 | 6188/4632/3967 | 1099 | 1317320 | 149 | A2 opt1 line (LOOP28 header baseline 82.3, md5 b62237e5) |
| opt1_rotoroff | `FBXPORT=1 TXTWRAM=1 LATESTEAL0=1 LATEKEEP=1 DRAWADOPT=1 PALROTOROFF=1` | c0c78277 | 87.3 | 83.0 | 96.6 | 100.0 | 62.8 | 5.5 |  | 10.7 | 8.0/8.0/8.0 | 3527/3562/3438 | 731 | 1257676 | 149 | A4 ceiling probe: no palette visits after vint 900 (colours freeze); LOOP28 86 read 94.7 |
