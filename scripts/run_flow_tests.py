#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Scripted UI flows that once hung or stopped the game.

    run_flow_tests.py <game_dir> [--renderer gl|gpu] [--out DIR] [--timeout S]

Each flow plays the real game headless (Xvfb, fixed time step, scripted
input) with a fresh user data directory whose path has upper case letters
(as the default ~/.local/share/MechCommander2 does), and must finish before
the timeout:

  options       main menu -> Options: the screen opens and frames keep coming
                (long graphics card names used to hang the text wrapping)
  quick_save    in a mission: quick load before any save (must not lock the
                mouse), quick save, quick load; the log must report both and
                no STOP / FATAL (lower-casing the user directory used to make
                every save write fail)
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "scripts"))
from run_screenshot_tests import FIXED_ARGS, start_xvfb  # noqa: E402


def combo(frame, key):
    """Ctrl+Alt+Shift+<key> (the in-mission quick save / load shortcuts)."""
    mods = ["Left Ctrl", "Left Alt", "Left Shift"]
    lines = ["%d keydown %s" % (frame, m) for m in mods]
    lines.append("%d keydown %s" % (frame + 2, key))
    lines.append("%d keyup %s" % (frame + 6, key))
    lines += ["%d keyup %s" % (frame + 6, m) for m in reversed(mods)]
    return lines


FLOWS = {
    # Options button of the main menu (800x600 window)
    "options": {
        "args": [],
        "script": ["590 move 402 349", "600 click left"],
        "frames": 700,
        "capture": 700,
        "expect": [],
    },
    "quick_save": {
        "args": ["-mission", "mc2_01"],
        "script": ["1 move 512 300"] + combo(300, "Z") + combo(400, "X") + combo(600, "Z"),
        "frames": 700,
        "capture": 700,
        "expect": ["Saved game", "Loaded game"],
    },
}

FORBIDDEN = ["STOP [", "FATAL", "ASSERT:", "VERIFY:", "MSGBOX:", "Segmentation"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    ap.add_argument("--renderer", default="gl")
    ap.add_argument("--out", default=None, help="logs and captures (default: temporary)")
    ap.add_argument("--timeout", type=int, default=300, help="seconds per flow")
    ap.add_argument("--only", help="comma separated flow names")
    args = ap.parse_args()

    game_dir = os.path.abspath(args.game_dir)
    out = os.path.abspath(args.out or tempfile.mkdtemp(prefix="mc2flows"))
    os.makedirs(out, exist_ok=True)
    names = args.only.split(",") if args.only else list(FLOWS)
    xvfb, display = start_xvfb("800x600")
    failed = []
    try:
        for name in names:
            flow = FLOWS[name]
            work = os.path.join(out, name)
            shutil.rmtree(work, ignore_errors=True)
            user_dir = os.path.join(work, "User Data", "MechCommander2")
            os.makedirs(user_dir)
            for cfg in ("options.cfg", "orgprefs.cfg"):
                shutil.copy(os.path.join(game_dir, "minprefs.cfg"), os.path.join(user_dir, cfg))
            script = os.path.join(work, "input.txt")
            with open(script, "w") as f:
                f.write("\n".join(flow["script"]) + "\n")
            env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER="dummy", MC2_NO_DIALOGS="1",
                       MC2_NO_MOVIES="1", MC2_USER_DIR=user_dir)
            cmd = ([os.path.join(game_dir, "mc2")] + flow["args"] + FIXED_ARGS +
                   ["-renderer", args.renderer, "-input-script", script,
                    "-exit-after", str(flow["frames"]),
                    "-capture-frames", str(flow["capture"]), "-capture-dir", work])
            log_path = os.path.join(work, "game.log")
            with open(log_path, "w") as log:
                try:
                    status = subprocess.call(cmd, cwd=game_dir, env=env, stdout=log,
                                             stderr=subprocess.STDOUT, timeout=args.timeout)
                except subprocess.TimeoutExpired:
                    status = "timeout (the game hung)"
            text = open(log_path, errors="replace").read()
            problems = []
            if status != 0:
                problems.append("exit status %s" % status)
            if not os.path.exists(os.path.join(work, "capture_%06d.png" % flow["capture"])):
                problems.append("no capture of frame %d" % flow["capture"])
            problems += ["log lacks %r" % e for e in flow["expect"] if e not in text]
            problems += ["log has %r" % b for b in FORBIDDEN if b in text]
            print("%-10s %s" % (name, "ok" if not problems else "FAIL: " + "; ".join(problems)))
            if problems:
                failed.append(name)
    finally:
        xvfb.kill()
    print("logs in", out)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
