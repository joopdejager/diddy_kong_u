#!/usr/bin/env python3
"""Run one scripted route on the x86-64 and big-endian PowerPC test builds and
compare them frame by frame. See docs/wiiu/big-endian-test.md.

Both builds must already exist (tools/wiiu/be-test.sh builds build-betest;
build-hosttest is the same configuration built with the host compiler).

What is compared, per frame:
  - the [PACE] racer line: position, race clock, checkpoint, lap, finish flag
  - the null renderer's summary: draw calls, triangles, vertex-layout checks
    (not its texture/shader creation totals: the texture cache is keyed on host
    memory addresses, which differ between 64- and 32-bit builds)
  - the set of colour-combiner shaders the game asked for
Gameplay floats are bit-reproducible across hosts (the project builds with
-ffp-contract=off), so any difference means the big-endian build computed
something differently, most likely from data read in the wrong byte order.

Usage: be_compare.py ROM SCRIPT FRAMES [ENV=VALUE ...]
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACE = re.compile(r"\[PACE\] frame=(\d+).*\| (racer .*)")
GFX = re.compile(r"\[gfx-null\] frame (\d+): (\d+ draws, \d+ triangles).*(layout \d+/\d+ bad)")
BAD = re.compile(r"LAYOUT MISMATCH|\[CRASH\]|\[FATAL\]")


def run(cmd, env, log):
    with open(log, "w") as out:
        return subprocess.call(cmd, env=env, stdout=out, stderr=subprocess.STDOUT)


def table(path, rx):
    rows = {}
    with open(path, errors="replace") as f:
        for line in f:
            m = rx.search(line)
            if m:
                rows[int(m.group(1))] = " ".join(m.groups()[1:])
    return rows


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    rom, script, frames = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), sys.argv[3]
    extra = dict(kv.split("=", 1) for kv in sys.argv[4:])
    work = tempfile.mkdtemp(prefix="be_compare_")
    logs = {}
    for name, exe, prefix in (
        ("x86-64", os.path.join(ROOT, "build-hosttest", "mdkr64"), []),
        ("ppc-be", os.path.join(ROOT, "build-betest", "mdkr64"),
         ["qemu-ppc", "-L", "/usr/powerpc-linux-gnu"]),
    ):
        env = {k: v for k, v in os.environ.items() if not k.startswith("MDKR_")}
        env.update(MDKR_TRACE="1", MDKR_AUDIO="0",
                   MDKR_SAVE_DIR=os.path.join(work, name, "save"),
                   MDKR_VIDEO_CONFIG_PATH=os.devnull)
        env.update(extra)
        os.makedirs(env["MDKR_SAVE_DIR"], exist_ok=True)
        log = os.path.join(work, name + ".log")
        code = run(prefix + [exe, "--rom", rom, "--headless-frames", frames,
                             "--input-script", script], env, log)
        logs[name] = log
        print(f"{name}: exit {code}, log {log}")
    failures = 0
    for label, rx in (("racer", PACE), ("renderer", GFX)):
        a, b = table(logs["x86-64"], rx), table(logs["ppc-be"], rx)
        common = sorted(set(a) & set(b))
        diff = [f for f in common if a[f] != b[f]]
        print(f"{label}: x86-64 {len(a)} frames, ppc-be {len(b)} frames, "
              f"{len(common)} compared, {len(diff)} differ")
        if len(a) != len(b) or diff:
            failures += 1
        for f in diff[:3]:
            print(f"  frame {f}\n    x86-64: {a[f]}\n    ppc-be: {b[f]}")
    shaders = []
    for name in ("x86-64", "ppc-be"):
        with open(logs[name], errors="replace") as f:
            shaders.append({l.split("shader ")[1].strip() for l in f if "[gfx-null] new shader" in l})
    print(f"shaders: x86-64 {len(shaders[0])}, ppc-be {len(shaders[1])}, "
          f"only x86-64 {sorted(shaders[0] - shaders[1])}, only ppc-be {sorted(shaders[1] - shaders[0])}")
    if shaders[0] != shaders[1]:
        failures += 1
    with open(logs["ppc-be"], errors="replace") as f:
        bad = [l.strip() for l in f if BAD.search(l)]
    if bad:
        failures += 1
        print("ppc-be problems:", *bad[:5], sep="\n  ")
    last = table(logs["ppc-be"], PACE)
    if last:
        print("last ppc-be racer row:", last[max(last)])
    print("PASS" if failures == 0 else "FAIL")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
