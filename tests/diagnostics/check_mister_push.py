#!/usr/bin/env python3
"""Exercise launch-log freshness with fake local commands; never contact a rig."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

SCRIPT = Path(__file__).resolve().parents[2] / "tools/mister_push.sh"
FAKE = r'''#!/usr/bin/env python3
import json, os, subprocess, sys
from pathlib import Path
kind = Path(sys.argv[0]).name
root = Path(os.environ["PUSH_TEST_ROOT"])
mode = os.environ["PUSH_TEST_MODE"]
log = root / "remote.log"
with (root / "calls.jsonl").open("a") as out:
    out.write(json.dumps([kind, *sys.argv[1:]]) + "\n")
if kind == "scp":
    sys.exit(1 if mode == "scp_error" else 0)
if kind == "sleep":
    if mode == "delayed":
        count_file = root / "sleeps"
        count = int(count_file.read_text()) + 1 if count_file.exists() else 1
        count_file.write_text(str(count))
        if count == 3:
            with log.open("a") as out: out.write("game started: S32X/target.32x\n")
    sys.exit(0)
if kind == "stat":
    s = Path(sys.argv[-1]).stat()
    print(f"{s.st_dev}:{s.st_ino}:{s.st_size}")
    sys.exit(0)
if kind == "curl":
    assert "-fsS" in sys.argv
    payload = json.loads(sys.argv[sys.argv.index("-d") + 1])
    name = Path(payload["path"]).name
    (root / "payload.json").write_text(json.dumps(payload))
    if mode == "curl_error": sys.exit(22)
    if mode == "rotate":
        log.rename(root / "old.log")
        log.write_text("game started: S32X/" + name + "\n")
    elif mode == "truncate":
        log.write_text("game started: S32X/" + name + "\n")
    elif mode in ("fresh", "missing", "many", "local_spaces", "elapsed7028"):
        with log.open("a") as out:
            seconds = "7028" if mode == "elapsed7028" else "0"
            out.write("2026/11/07 00:43:01 INFO game started: Sega32X/" + name + " (" + seconds + "s)\n")
            if mode == "many": out.write("core started\n" * 30)
    elif mode == "legacy":
        with log.open("a") as out: out.write("game started: S32X/" + name + "\n")
    elif mode == "wrong":
        with log.open("a") as out: out.write("2026/11/07 00:43:01 INFO game started: Sega32X/wrong.32x (0s)\n")
    elif mode == "wrong_suffix":
        with log.open("a") as out: out.write("2026/11/07 00:43:01 INFO game started: Sega32X/" + name + " (7.2s)\n")
    elif mode == "wrong_suffix_tail":
        with log.open("a") as out: out.write("2026/11/07 00:43:01 INFO game started: Sega32X/" + name + " (0s) junk\n")
    elif mode == "suffix":
        with log.open("a") as out: out.write("game started: S32X/" + name + ".bak\n")
    elif mode == "substring":
        with log.open("a") as out: out.write("game started: S32X/prefix_" + name + "\n")
    elif mode == "partial":
        with log.open("a") as out: out.write("me started: S32X/" + name + "\n")
    elif mode == "incomplete":
        with log.open("a") as out: out.write("game started: S32X/" + name)
    sys.exit(0)
if kind == "ssh":
    command = sys.argv[-1]
    if command.startswith("if [") and mode == "baseline_error": sys.exit(1)
    if command.startswith("sh -c") and mode == "poll_error": sys.exit(1)
    if command.startswith("if [") and mode == "bad_boundary":
        print("1::")
        sys.exit(0)
    command = command.replace("/tmp/remote.log", str(log))
    sys.exit(subprocess.run(["/bin/sh", "-c", command]).returncode)
raise SystemExit("unexpected fake " + kind)
'''


def check(mode, *, expect=0, options=(), dest="target.32x", named=True):
    with tempfile.TemporaryDirectory(prefix="mister-push-") as tmp:
        root = Path(tmp)
        bindir = root / "bin"
        bindir.mkdir()
        for command in ("ssh", "scp", "curl", "sleep", "stat"):
            file = bindir / command
            file.write_text(FAKE)
            file.chmod(0o755)
        rom = root / ("local source.32x" if mode == "local_spaces" else "source.32x")
        rom.write_bytes(b"fake rom")
        if mode != "missing":
            (root / "remote.log").write_text(
                "2026/11/07 00:43:01 INFO game started: Sega32X/target.32x (7028s)\n" + "old trailing message\n" * 8
                + ("ga" if mode == "partial" else ""))
        env = os.environ.copy()
        for key in ("MISTER", "MISTER_HOST", "MISTER_DIR"):
            env.pop(key, None)
        env.update(PATH=str(bindir) + os.pathsep + env["PATH"],
                   PUSH_TEST_ROOT=str(root), PUSH_TEST_MODE=mode)
        args = ["sh", str(SCRIPT)]
        if named:
            args += ["--name", "test.invalid"]
        args += [*options, str(rom), dest]
        result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=15)
        assert result.returncode == expect, (mode, result.returncode, result.stdout, result.stderr)
        calls_file = root / "calls.jsonl"
        calls = [json.loads(line) for line in calls_file.read_text().splitlines()] if calls_file.exists() else []
        names = [call[0] for call in calls]
        if expect == 0 and "--no-launch" not in options:
            assert "launch log confirmed:" in result.stdout, (mode, result.stdout)
            assert "cold-boot state are not verified" in result.stdout
            assert names.index("ssh") < names.index("curl")
            assert json.loads((root / "payload.json").read_text())["path"].endswith("/" + dest)
        else:
            assert "launch log confirmed:" not in result.stdout
        if "--no-launch" in options:
            assert names == ["scp"], names
        if mode in ("scp_error", "baseline_error", "bad_boundary") or not named or expect == 2:
            assert "curl" not in names, names
        if mode in ("stale", "wrong", "suffix", "substring", "partial"):
            assert names.count("sleep") == 10, names
        if mode == "delayed":
            assert names.count("sleep") == 3, names
        if mode in ("fresh", "elapsed7028"):
            seconds = "7028" if mode == "elapsed7028" else "0"
            assert "2026/11/07 00:43:01 INFO game started: Sega32X/target.32x (" + seconds + "s)" in result.stdout
        if "scp" in names:
            call = next(call for call in calls if call[0] == "scp")
            assert call[1] == "-q" and "-O" not in call, call


for mode in ("fresh", "missing", "many", "local_spaces", "delayed", "elapsed7028", "legacy"):
    check(mode)
for mode in ("stale", "wrong", "suffix", "substring", "partial", "rotate", "truncate",
             "curl_error", "scp_error", "baseline_error", "poll_error", "bad_boundary", "incomplete", "wrong_suffix", "wrong_suffix_tail"):
    check(mode, expect=1)
check("no_launch", options=("--no-launch",))
check("no_target", named=False, expect=2)
for name in ("../target.32x", "target's.32x", "target.32x;bad", "target file.32x", "target\nfile.32x"):
    check("unsafe", dest=name, expect=2)
missing_arg = subprocess.run(["sh", str(SCRIPT), "--name"], capture_output=True, text=True)
assert missing_arg.returncode == 2
print("mister_push: 30 fake-command launch/freshness/error/quoting cases passed")
