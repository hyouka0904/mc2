#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Frame time benchmark.

    benchmark.py <game_dir> [--runs N] [--frames N] [--missions m1,m2]
                 [--renderer gl|gpu] [--json out.json]

Plays each mission in deterministic fixed-step mode (the same settings as the
screenshot tests: -fixedstep 33 -seed 1, Xvfb, dummy audio), so every run
simulates and draws exactly the same frames. The game writes per-frame wall
clock and main-thread CPU times with -perf-csv; the first 100 frames
(loading, first uploads) are left out. For each mission the median over the
runs of the mean frame time, the main thread's CPU time per frame and the
draw calls per frame are reported.

Main-thread CPU time is the number to watch for game and renderer code: it
is what limits the frame rate on a real GPU. The wall-clock time also
includes waiting for the GPU.

Without a GPU the SDL3 GPU backend runs on Mesa lavapipe, so the numbers
include software rasterisation. Compare runs on the same machine only.
"""

import argparse
import json
import os
import statistics
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "scripts"))
from run_screenshot_tests import FIXED_ARGS, prepare_user_dir, start_xvfb  # noqa: E402

SKIP_FRAMES = 100


def run_once(game_dir, display, mission, frames, renderer, tmp, exe="mc2"):
    csv_path = os.path.join(tmp, "perf_%s.csv" % mission)
    env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER="dummy", MC2_NO_DIALOGS="1",
               MC2_NO_MOVIES="1", MC2_USER_DIR=prepare_user_dir(game_dir, os.path.join(tmp, "userdir")))
    cmd = [os.path.join(game_dir, exe)] + FIXED_ARGS + [
        "-mission", mission, "-exit-after", str(frames), "-perf-csv", csv_path]
    if renderer:
        cmd += ["-renderer", renderer]
    subprocess.run(cmd, cwd=game_dir, env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=900, check=False)
    ms, cpu, draws = [], [], []
    with open(csv_path) as f:
        header = f.readline().strip().split(",")
        i_ms = header.index("frame_ms")
        i_cpu = header.index("cpu_ms") if "cpu_ms" in header else None
        i_draw = header.index("draw_calls")
        for line in f:
            cols = line.strip().split(",")
            if len(cols) < len(header) or int(cols[0]) <= SKIP_FRAMES:
                continue
            ms.append(float(cols[i_ms]))
            draws.append(float(cols[i_draw]))
            if i_cpu is not None:
                cpu.append(float(cols[i_cpu]))
    if len(ms) < frames - SKIP_FRAMES - 5:
        raise SystemExit("%s: only %d frames recorded" % (mission, len(ms)))
    return (statistics.mean(ms), statistics.median(ms), statistics.mean(draws),
            statistics.mean(cpu) if cpu else 0.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--missions", default="mc2_01,mc2_07")
    ap.add_argument("--renderer", default="gpu")
    ap.add_argument("--json")
    ap.add_argument("--exe", default="mc2",
                    help="executable(s) in game_dir, comma separated: several are run "
                         "alternately, so machine load affects them alike")
    args = ap.parse_args()
    exes = args.exe.split(",")

    game_dir = os.path.abspath(args.game_dir)
    xvfb, display = start_xvfb("800x600")
    results = {e: {} for e in exes}
    try:
        with tempfile.TemporaryDirectory() as tmp:
            for mission in args.missions.split(","):
                runs = {e: [] for e in exes}
                for _ in range(args.runs):
                    for e in exes:
                        runs[e].append(run_once(game_dir, display, mission, args.frames,
                                                args.renderer, tmp, e))
                for e in exes:
                    r = runs[e]
                    res = {"mean_ms": statistics.median(x[0] for x in r),
                           "median_ms": statistics.median(x[1] for x in r),
                           "draws": r[0][2],
                           "cpu_ms": statistics.median(x[3] for x in r),
                           "runs_mean_ms": [round(x[0], 3) for x in r],
                           "runs_cpu_ms": [round(x[3], 3) for x in r]}
                    results[e][mission] = res
                    print("%-10s %-8s wall %7.3f ms (median %7.3f)  main-thread CPU %6.3f ms"
                          "  draws/frame %6.1f  runs cpu %s" % (
                              e, mission, res["mean_ms"], res["median_ms"], res["cpu_ms"],
                              res["draws"], " ".join("%.3f" % x for x in res["runs_cpu_ms"])))
    finally:
        xvfb.kill()
    for e in exes:
        print("%-10s total    wall %7.3f ms  main-thread CPU %6.3f ms" % (
            e, sum(r["mean_ms"] for r in results[e].values()),
            sum(r["cpu_ms"] for r in results[e].values())))
    if args.json:
        with open(args.json, "w") as f:
            json.dump(results if len(exes) > 1 else results[exes[0]], f, indent=1)


if __name__ == "__main__":
    main()
