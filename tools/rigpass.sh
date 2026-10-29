#!/bin/zsh
# rigpass.sh <rom> [name] : launch on the office rig, capture every 8 s for ~2 min, write a montage
R=$1; N=${2:-$(basename $R .32x)}; D=${ACCEPT_OUT:-$(dirname $0)/../build/accept}/rig/pass_$N; mkdir -p $D; M=root@mister.office.local
cd /Users/mikeholzinger/src/sega16-2-32x/.claude/worktrees/advisor-bisect
# The rigs live on the home LAN (mister.office.local = 10.0.4.x). Off that subnet (Mike's work
# office, weekdays) the rig cannot answer and the strip would fail half way: say so and stop.
MYIP=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null)
if ! ssh -o ConnectTimeout=6 -o BatchMode=yes $M true 2>/dev/null; then
  echo "rigpass: rig unreachable from ${MYIP:-unknown} (home LAN is 10.0.4.x): not at home, strip skipped"; exit 2
fi
tools/mister_push.sh --name mister.office.local $R 2>&1 | tail -1
T0=$(date +%s)
for k in $(seq 1 14); do
  sleep 8
  ssh -o ConnectTimeout=8 $M 'echo screenshot > /dev/MiSTer_cmd'; sleep 2
  F=$(ssh -o ConnectTimeout=8 $M 'ls -t /media/fat/screenshots/S32X/*.png | head -1')
  scp -q $M:"$F" $D/$(printf '%02d' $k)_$(( $(date +%s) - T0 ))s.png
done
python3 - "$D" <<'PY'
import sys, os, glob
from PIL import Image, ImageDraw
D=sys.argv[1]; fs=sorted(glob.glob(D+'/*.png'))
ims=[Image.open(f).convert('RGB') for f in fs]
cols=5; rows=(len(ims)+cols-1)//cols
out=Image.new('RGB',(320*cols,224*rows)); d=ImageDraw.Draw(out)
for i,(im,f) in enumerate(zip(ims,fs)):
    out.paste(im,((i%cols)*320,(i//cols)*224)); d.rectangle(((i%cols)*320,(i//cols)*224,(i%cols)*320+60,(i//cols)*224+12),fill=(0,0,0)); d.text(((i%cols)*320+2,(i//cols)*224+1),os.path.basename(f)[:7],fill=(255,255,0))
out.save(D+'/montage.png'); print(D+'/montage.png')
PY
