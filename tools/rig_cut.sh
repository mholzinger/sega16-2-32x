#!/bin/zsh
# rig_cut.sh <rom> [name] [from_s] [to_s] : launch a rom on the rig and screenshot it every ~1.5 s
# between from_s and to_s after launch (default 15..60, the level-1 transformation on a
# PORTTAPEPAD+PORTFORM probe lands ~28-40 s in), then pull every shot the rig took of that rom
# since the launch into build/accept/rig/cut_<name>/ and write a montage. 2026-11-05.
set -e
R=$1; N=${2:-$(basename $R .32x)}; FROM=${3:-15}; TO=${4:-60}
rig_cut_launch_confirmed=0
D=${ACCEPT_OUT:-$(dirname $0)/../build/accept}/rig/cut_$N; mkdir -p $D; M=root@mister.office.local
cd "$(dirname "$0")/.."
if ! ssh -o ConnectTimeout=6 -o BatchMode=yes $M true 2>/dev/null; then echo "rig_cut: rig unreachable"; exit 2; fi
# Restore even if local capture/copy processing fails. A probe must not leave
# Mike with tape-controlled input when he resumes play.
rig_cut_restore() {
  local capture_status=$? restore_status=0 selected
  trap - EXIT
  if [ -n "$RIG_RESTORE" ]; then
    if [[ $rig_cut_launch_confirmed == 1 && "${R:A}" == "${RIG_RESTORE:A}" ]]; then
      # An accepted-only restore cut already launched this exact ROM. The
      # launch API may deduplicate a second request and emit no fresh log.
      echo "rig_cut: restore ROM already launched; duplicate request skipped" > "$D/restore.log"
    elif tools/mister_push.sh --name mister.office.local "$RIG_RESTORE" > "$D/restore.log" 2>&1; then
      tail -2 "$D/restore.log"
    else
      restore_status=$?
      tail -5 "$D/restore.log" >&2
      echo "rig_cut: RESTORE FAILED (status $restore_status); see $D/restore.log" >&2
    fi
  fi
  selected=$(ssh -o ConnectTimeout=8 $M 'cat /var/log/ACTIVEGAME' 2>/dev/null || true)
  echo "rig_cut: launcher-selected metadata only (display/cold boot unverified): ${selected:-unavailable}"
  if [ $restore_status -ne 0 ]; then exit 3; fi
  exit $capture_status
}
trap rig_cut_restore EXIT
ssh $M 'touch /tmp/rig_cut_mark'
if tools/mister_push.sh --name mister.office.local "$R" > "$D/launch.log" 2>&1; then
  rig_cut_launch_confirmed=1
  tail -2 "$D/launch.log"
else
  launch_status=$?
  tail -5 "$D/launch.log" >&2
  echo "rig_cut: launch failed (status $launch_status); capture aborted" >&2
  exit $launch_status
fi
T0=$(date +%s)
while true; do
  T=$(( $(date +%s) - T0 ))
  if [ $T -ge $TO ]; then break; fi
  if [ $T -ge $FROM ]; then ssh -o ConnectTimeout=8 $M 'echo screenshot > /dev/MiSTer_cmd'; sleep 2; else sleep 1; fi   # the rig's screenshot takes ~5 s; the FIFO serialises
done
sleep 3
ROMB=$(basename $R)
ssh $M "cd /media/fat/screenshots/S32X && find . -newer /tmp/rig_cut_mark -name '*${ROMB%.32x}*.png' | sed 's#^\./##'" > $D/list.txt 2>/dev/null
rm -f "${D:?}"/[0-9]*.png(N)
k=0
for f in $(sort $D/list.txt); do k=$((k+1)); scp -q $M:/media/fat/screenshots/S32X/$f $D/$(printf '%02d' $k)_$f; done
echo "rig_cut: $k shots -> $D"
# THE RIG KEEPS WHATEVER WAS LAUNCHED LAST. A probe (baked tape, god mode) left here after a
# capture is what Mike picks up next: 2026-11-05 he played p15bpt2ft6 believing it was p15bpt4
# ("ignores input and is stuck at neff in a loop"). RIG_RESTORE=<rom> relaunches the candidate.
python3 - "$D" <<'PY'
import sys, os, glob
from PIL import Image, ImageDraw
D=sys.argv[1]; fs=sorted(glob.glob(D+'/[0-9]*.png'))
ims=[Image.open(f).convert('RGB') for f in fs]
if not ims: sys.exit(0)
cols=5; rows=(len(ims)+cols-1)//cols
out=Image.new('RGB',(320*cols,224*rows)); d=ImageDraw.Draw(out)
for i,(im,f) in enumerate(zip(ims,fs)):
    im=im.resize((320,224)) if im.size!=(320,224) else im
    out.paste(im,((i%cols)*320,(i//cols)*224)); d.rectangle(((i%cols)*320,(i//cols)*224,(i%cols)*320+30,(i//cols)*224+12),fill=(0,0,0)); d.text(((i%cols)*320+2,(i//cols)*224+1),os.path.basename(f)[:2],fill=(255,255,0))
out.save(D+'/montage.png'); print(D+'/montage.png')
PY
