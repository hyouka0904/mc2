#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deterministic screenshot tests.

    run_screenshot_tests.py <game_dir> <out_dir> [--update] [--only NAME...]
                            [--renderer gl|gpu] [--gpu-validation]

Runs the game headless (Xvfb, Mesa software GL, dummy audio) in fixed-step
mode for every row of tests/golden/manifest.txt, saves the captured frames
to <out_dir>, then compares them with the golden images in tests/golden
(see compare_screenshots.py for thresholds). Heat maps of failing images go
to <out_dir>/diff.

--update          copy the new captures over the golden images instead of comparing
--only            restrict to the named images
--renderer        graphics backend passed to the game (default gl). The golden
                  images come from the OpenGL backend; the SDL3 GPU backend
                  (Vulkan on Mesa lavapipe) is compared against the same images.
--gpu-validation  run the SDL3 GPU backend with the Vulkan validation layers
                  and fail if any validation message is logged

Exit status: 0 pass, 1 mismatch or capture failure, 2 setup error.
"""

import argparse
import os
import random
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from collections import OrderedDict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLDEN = os.path.join(REPO, "tests", "golden")
sys.path.insert(0, os.path.join(REPO, "scripts"))
import compare_screenshots  # noqa: E402

FIXED_ARGS = ["-fixedstep", "33", "-seed", "1", "-windowed"]


def read_manifest(path):
    rows = []
    with open(path) as f:
        for line_no, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = shlex.split(line)
            if len(parts) < 2 or not parts[1].isdigit():
                raise SystemExit("%s:%d: expected '<name> <frame> [args]'" % (path, line_no))
            args = [os.path.join(GOLDEN, a[1:]) if a.startswith("@") else a for a in parts[2:]]
            rows.append((parts[0], int(parts[1]), args))
    return rows


def start_xvfb(screen):
    for _ in range(20):
        display = ":%d" % random.randint(100, 400)
        proc = subprocess.Popen(["Xvfb", display, "-screen", "0", screen + "x24", "-nolisten", "tcp"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            if subprocess.call(["xdpyinfo", "-display", display],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
                return proc, display
            if proc.poll() is not None:
                break
            time.sleep(0.1)
        proc.kill()
    raise SystemExit("could not start Xvfb")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    ap.add_argument("out_dir")
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--timeout", type=int, default=900)
    # The game window is 800x600; a screen of the same size keeps captures
    # identical whether or not the window manager honours fullscreen.
    ap.add_argument("--screen", default="800x600")
    ap.add_argument("--renderer", choices=["gl", "gpu"], default="gl")
    ap.add_argument("--gpu-validation", action="store_true")
    ap.add_argument("--gpu-shapes", action="store_true",
                    help="draw 3D models with the GPU path (-gpu-shapes)")
    args = ap.parse_args()

    for tool in ("Xvfb", "xdpyinfo"):
        if not shutil.which(tool):
            print("missing %s (install xvfb x11-utils)" % tool)
            return 2

    game_dir = os.path.abspath(args.game_dir)
    out_dir = os.path.abspath(args.out_dir)
    os.makedirs(out_dir, exist_ok=True)

    rows = read_manifest(os.path.join(GOLDEN, "manifest.txt"))
    if args.only:
        rows = [r for r in rows if r[0] in args.only]

    runs = OrderedDict()  # args tuple -> [(name, frame)]
    for name, frame, game_args in rows:
        runs.setdefault(tuple(game_args), []).append((name, frame))

    xvfb, display = start_xvfb(args.screen)
    env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER="dummy", LIBGL_ALWAYS_SOFTWARE="1",
               MC2_NO_DIALOGS="1")
    if args.gpu_validation:
        env["MC2_GPU_DEBUG"] = "1"
    fixed_args = FIXED_ARGS + ["-renderer", args.renderer]
    if args.gpu_shapes:
        fixed_args.append("-gpu-shapes")
    failed = False
    try:
        if not os.path.exists(os.path.join(game_dir, "options.cfg")):
            print("First launch: running the one-time hardware check")
            subprocess.call(["./mc2"], cwd=game_dir, env=env, timeout=120,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        for run_index, (game_args, shots) in enumerate(runs.items()):
            frames = ",".join(str(f) for _, f in shots)
            label = " ".join(game_args) or "(no arguments)"
            print("run %d/%d: %s  frames %s" % (run_index + 1, len(runs), label, frames))
            tmp = tempfile.mkdtemp(prefix="mc2cap")
            cmd = ["./mc2"] + list(game_args) + fixed_args + ["-capture-frames", frames, "-capture-dir", tmp]
            log_path = os.path.join(out_dir, "run%02d.log" % (run_index + 1))
            with open(log_path, "w") as log:
                try:
                    status = subprocess.call(cmd, cwd=game_dir, env=env, stdout=log,
                                             stderr=subprocess.STDOUT, timeout=args.timeout)
                except subprocess.TimeoutExpired:
                    status = "timeout"
            if status != 0:
                print("  FAIL: game exited with %s (log: %s)" % (status, log_path))
                failed = True
            if args.gpu_validation:
                with open(log_path, errors="replace") as log:
                    messages = [l.strip() for l in log if "Validation" in l or "VUID-" in l]
                if messages:
                    print("  FAIL: %d Vulkan validation messages, first: %s" % (len(messages), messages[0][:300]))
                    failed = True
            for name, frame in shots:
                src = os.path.join(tmp, "capture_%06d.png" % frame)
                if os.path.exists(src):
                    shutil.move(src, os.path.join(out_dir, name + ".png"))
                else:
                    print("  FAIL: %s (frame %d) was not captured" % (name, frame))
                    failed = True
            shutil.rmtree(tmp, ignore_errors=True)
    finally:
        xvfb.kill()

    names = [r[0] for r in rows]
    if args.update:
        for name in names:
            src = os.path.join(out_dir, name + ".png")
            if os.path.exists(src):
                shutil.copy(src, os.path.join(GOLDEN, name + ".png"))
        print("updated %d golden images in %s" % (len(names), GOLDEN))
        return 1 if failed else 0

    # compare only the selected images
    with tempfile.TemporaryDirectory() as sel:
        for name in names:
            g = os.path.join(GOLDEN, name + ".png")
            if os.path.exists(g):
                os.symlink(g, os.path.join(sel, name + ".png"))
            else:
                print("MISSING golden image %s.png (run with --update)" % name)
                failed = True
        result = compare_screenshots.main([sel, out_dir, "--diff-dir", os.path.join(out_dir, "diff")])
    return 1 if (failed or result) else 0


if __name__ == "__main__":
    sys.exit(main())
