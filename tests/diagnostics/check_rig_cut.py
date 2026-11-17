#!/usr/bin/env python3
"""Fake-command integration of launch failure, capture gating and restore status."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FAKE = r'''#!/usr/bin/env python3
import json,os,subprocess,sys
from pathlib import Path
kind=Path(sys.argv[0]).name;root=Path(os.environ['CUT_TEST_ROOT']);mode=os.environ['CUT_TEST_MODE'];log=root/'remote.log'
with (root/'calls.jsonl').open('a') as f:f.write(json.dumps([kind,*sys.argv[1:]])+'\n')
if kind in ('scp','sleep'):sys.exit(0)
if kind=='stat':
 s=Path(sys.argv[-1]).stat();print(f'{s.st_dev}:{s.st_ino}:{s.st_size}');sys.exit(0)
if kind=='curl':
 name=Path(json.loads(sys.argv[sys.argv.index('-d')+1])['path']).name
 if (name=='probe.32x' and mode in ('launch_fail','both_fail')) or (name=='restore.32x' and mode in ('restore_fail','both_fail')):sys.exit(22)
 with log.open('a')as f:f.write('2026/11/07 00:43:01 INFO game started: Sega32X/'+name+' (0s)\n')
 sys.exit(0)
if kind=='ssh':
 command=sys.argv[-1]
 if command=='true' or command=='touch /tmp/rig_cut_mark':sys.exit(0)
 if command=='cat /var/log/ACTIVEGAME':print('stale-selected-metadata.32x');sys.exit(0)
 if command.startswith('cd /media/fat/screenshots/'):
  if mode=='capture_fail':sys.exit(1)
  sys.exit(0)
 if 'echo screenshot' in command:sys.exit(0)
 if command.startswith('if [') or command.startswith('sh -c'):
  sys.exit(subprocess.run(['/bin/sh','-c',command.replace('/tmp/remote.log',str(log))]).returncode)
 raise SystemExit('unexpected remote command '+command)
raise SystemExit('unexpected fake '+kind)
'''

for mode,expected in [('launch_fail',1),('restore_fail',3),('both_fail',3),('normal',0),('capture_fail',1)]:
    with tempfile.TemporaryDirectory(prefix='rig-cut-') as tmp:
        root=Path(tmp);bindir=root/'bin';bindir.mkdir()
        for name in ('ssh','scp','curl','sleep','stat'):
            p=bindir/name;p.write_text(FAKE);p.chmod(0o755)
        (root/'remote.log').write_text('2026/11/07 00:43:01 INFO game started: Sega32X/probe.32x (7028s)\n')
        for name in ('probe.32x','restore.32x'):(root/name).write_bytes(b'test rom')
        env=os.environ.copy()
        for name in ('MISTER','MISTER_HOST','MISTER_DIR'):env.pop(name,None)
        env.update(PATH=str(bindir)+os.pathsep+env['PATH'],CUT_TEST_ROOT=str(root),CUT_TEST_MODE=mode,
                   ACCEPT_OUT=str(root/'accept'),RIG_RESTORE=str(root/'restore.32x'))
        result=subprocess.run(['zsh',str(ROOT/'tools/rig_cut.sh'),str(root/'probe.32x'),'test','0','0'],cwd=ROOT,env=env,capture_output=True,text=True,timeout=20)
        assert result.returncode==expected,(mode,result.returncode,result.stdout,result.stderr)
        calls=[json.loads(line)for line in (root/'calls.jsonl').read_text().splitlines()]
        launches=[Path(json.loads(c[c.index('-d')+1])['path']).name for c in calls if c[0]=='curl']
        assert launches==['probe.32x','restore.32x'],(mode,launches)
        assert 'loaded on the rig now' not in result.stdout
        assert 'launcher-selected metadata only' in result.stdout
        directory=root/'accept/rig/cut_test'
        assert (directory/'launch.log').exists() and (directory/'restore.log').exists()
        if mode in ('launch_fail','both_fail'):
            assert 'capture aborted' in result.stderr
            assert not (directory/'list.txt').exists()
            assert not any('screenshot' in c[-1] for c in calls if c[0]=='ssh')
        if mode in ('restore_fail','both_fail'):
            assert 'RESTORE FAILED' in result.stderr
        else:
            assert 'RESTORE FAILED' not in result.stderr
        if mode=='normal':assert '0 shots ->' in result.stdout
print('rig_cut: 5 fake-command capture/restore integration cases passed')
