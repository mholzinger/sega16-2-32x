import csv,sys
# args: log.csv ; columns: frame,gameframe,slots16-39 (0xC00 hex, every 10 frames),level byte pair
rows=[r for r in csv.reader(open(sys.argv[1]))][1:]
t={}; by={}
for r in rows:
    if len(r)<4: continue
    f=int(r[0])
    if f in (3000,3400,5000,5400): t[f]=r[1]
    if r[2] and r[3]:
        lvl=int(r[3][:2],16); b=bytes.fromhex(r[2])
        d=by.setdefault(lvl,{})
        d.setdefault('_frames',[]).append(f)
        for s in range(24):
            h=int.from_bytes(b[s*0x80+2:s*0x80+6],'big')
            if h: d[h]=d.get(h,0)+1
print("ticks: " + ' '.join(f"@{k}={v}" for k,v in sorted(t.items())))
for lvl,d in sorted(by.items()):
    fr=d.pop('_frames'); print(f"  level byte {lvl}: frames {fr[0]}-{fr[-1]} ({len(fr)} samples): " + ' '.join(f"{h:05x}({n})" for h,n in sorted(d.items())))
