#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Scripted UI flows through every screen of the game.

    run_flow_tests.py <game_dir> [--renderer gl|gpu] [--out DIR] [--timeout S]
                      [--only NAME,...]

Each flow plays the real game headless (Xvfb 800x600, fixed time step,
scripted mouse and keys) with a fresh user data directory whose path has
upper case letters and a space (as ~/.local/share/MechCommander2 or
"Application Support" do), and must finish before the timeout without a
STOP, assertion or crash in the log:

  options        main menu -> Options: every tab, Accept keeps the resolution
  encyclopedia   every section, Close
  exit           Exit -> Yes quits
  campaign       new campaign, win the first mission (debug key), results,
                 briefing, 'mech bay, pilots, launch of the second mission
  load_game      Load Game lists the campaign save and loads it
  solo_mission   Solo Mission lists the completed mission; briefing, 'mech bay
                 and pilot screens of that mission
  quick_save     quick load before any save, quick save, quick load (keys)
  pause_menu     after a box selection: pause menu Quick Save, Quick Load,
                 Options -> Accept, Exit -> Yes

  old_campaign_save  Load Game with the campaign save of every release in
                 tests/saves (the oldest format must keep loading)
  old_quick_save     quick load of a release's in-mission save
  foreign_quick_save quick load of a save from another build: refused with a
                 message, the mission goes on

load_game and solo_mission use the save the campaign flow leaves. A
capture every 100 frames (capture_NNNNNN.png) and game.log are kept in
<out>/<flow>.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from collections import OrderedDict

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


def click(frame, x, y):
    return ["%d move %d %d" % (frame - 10, x, y), "%d click left" % frame]


# Window coordinates (800x600) of the buttons the flows use
MAIN_MENU = {"campaign": (402, 138), "load": (402, 180), "solo": (402, 306),
             "options": (402, 349), "encyclopedia": (402, 391), "exit": (402, 476)}
DIALOG_YES = (474, 394)      # quit / abort confirmation
NEXT = (730, 564)            # logistics screens: Next / Launch
PAUSE = {"quick_save": (728, 156), "quick_load": (728, 182), "options": (728, 208),
         "abort": (728, 234), "exit": (728, 262)}
OPTIONS_ACCEPT = (570, 544)


def menu(frame, name):
    return click(frame, *MAIN_MENU[name])


FLOWS = OrderedDict()

# Options: every tab, then Accept without changes back to the main menu
# (the list item wrapping used to hang on long graphics card names; Accept
# used to change the resolution because SDL reports 24 bit modes)
FLOWS["options"] = {
    "args": [],
    "script": menu(600, "options") + click(700, 300, 144) + click(800, 480, 144) +
              click(900, 580, 144) + click(1000, 166, 144) + click(1100, *OPTIONS_ACCEPT),
    "frames": 1200,
    "expect_cfg": {"ResolutionX": "800", "ResolutionY": "600"},
}
# Encyclopedia: every section, then Close
FLOWS["encyclopedia"] = {
    "args": [],
    "script": menu(600, "encyclopedia") + click(700, 156, 110) + click(800, 252, 110) +
              click(900, 60, 200) + click(1000, 156, 200) + click(1100, 252, 200) +
              click(1200, 700, 566),
    "frames": 1300,
}
# Exit -> Yes quits the game well before the frame limit
FLOWS["exit"] = {
    "args": [],
    "script": menu(600, "exit") + click(700, *DIALOG_YES),
    "frames": 900,
    "quits": True,
}
# New campaign: first mission, won with the debug key, results, the
# logistics screens (briefing, 'mech bay, pilots) and the launch of the
# second mission. Leaves the campaign save the next two flows use.
FLOWS["campaign"] = {
    "args": [],
    "script": menu(600, "campaign") + click(710, 550, 436) + combo(1300, "W") +
              click(1700, 710, 536) + click(1910, *NEXT) + click(2110, *NEXT) +
              click(2310, *NEXT) + click(2510, *NEXT),
    "frames": 3000,
    "expect_files": ["savegame/After_Mission_mc2_01.fit"],
}
# Load game with that save: the list shows it, Load opens the logistics
FLOWS["load_game"] = {
    "args": [],
    "seed_from": "campaign",
    "script": menu(600, "load") + click(710, 550, 436) + click(910, *NEXT),
    "frames": 1200,
}
# Solo mission lists the completed campaign mission; Accept opens its
# briefing, then the 'mech bay and pilot screens
FLOWS["solo_mission"] = {
    "args": [],
    "seed_from": "campaign",
    "script": menu(600, "solo") + click(710, 720, 560) + click(1010, *NEXT) +
              click(1210, *NEXT) + click(1410, *NEXT),
    "frames": 1900,
}
# Saves written by released versions (tests/saves/<version>/) must keep
# loading. The quick save is a Linux x86_64 one: its layout is per platform.
SAVES = os.path.join(REPO, "tests", "saves")
FLOWS["old_campaign_save"] = {
    "args": [],
    "seed_dir": "*/After_Mission_mc2_01.fit",
    "script": menu(600, "load") + click(710, 550, 436) + click(910, *NEXT),
    "frames": 1200,
    "expect": ["[save] campaign save loaded (format 10004): ", " 3 mechs, 80000 C-Bills"],
    "forbid": ["[save] save game:"],
}
FLOWS["old_quick_save"] = {
    "args": ["-mission", "mc2_01"],
    "seed_dir": "0.1.0/testgame.ims",
    "script": ["1 move 512 300"] + combo(300, "Z"),
    "frames": 500,
    "expect": ["Loaded game"],
}
FLOWS["foreign_quick_save"] = {
    "args": ["-mission", "mc2_01"],
    "seed_dir": "0.1.0/testgame.ims",
    "foreign": True,
    "script": ["1 move 512 300"] + combo(300, "Z"),
    "frames": 500,
    "expect": ["[save] quick save", "is from another build (00.06.01.0615)"],
    "forbid": ["Loaded game"],
}
# Quick save / load shortcuts; a quick load before any save must not
# leave the mouse off (lower-casing the user directory used to make every
# save write fail)
FLOWS["quick_save"] = {
    "args": ["-mission", "mc2_01"],
    "script": ["1 move 512 300"] + combo(300, "Z") + combo(400, "X") + combo(600, "Z"),
    "frames": 700,
    "expect": ["Saved game", "Loaded game"],
}
# Pause menu after a box selection: Quick Save, Quick Load, Options ->
# Accept, Exit -> Yes (the first click after a drag was ignored, and
# Options -> Accept crashed in a mission started with -mission)
FLOWS["pause_menu"] = {
    "args": ["-mission", "mc2_01"],
    "script": ["1 move 300 200", "600 down left", "603 move 350 250", "606 move 450 400",
               "610 up left", "700 key Escape"] +
              click(750, *PAUSE["quick_save"]) + click(850, *PAUSE["quick_load"]) +
              ["1000 key Escape"] + click(1050, *PAUSE["options"]) +
              click(1200, *OPTIONS_ACCEPT) + ["1300 key Escape"] +
              click(1350, *PAUSE["exit"]) + click(1450, *DIALOG_YES),
    "frames": 1700,
    "expect": ["Saved game", "Loaded game"],
    "quits": True,
}

FORBIDDEN = ["STOP [", "FATAL", "ASSERT:", "VERIFY:", "MSGBOX:", "Segmentation"]


def check_cfg(path, expected):
    values = {}
    if os.path.exists(path):
        for line in open(path, errors="replace"):
            parts = line.split("=", 1)
            if len(parts) == 2 and len(parts[0].split()) == 2:
                values[parts[0].split()[1]] = parts[1].strip().strip('"')
    return ["options.cfg has %s = %s, not %s" % (k, values.get(k), v)
            for k, v in expected.items() if values.get(k) != v]


def run_flow(name, flow, game_dir, out, renderer, display, timeout):
    work = os.path.join(out, name)
    shutil.rmtree(work, ignore_errors=True)
    user_dir = os.path.join(work, "User Data", "MechCommander2")
    os.makedirs(user_dir)
    for cfg in ("options.cfg", "orgprefs.cfg"):
        shutil.copy(os.path.join(game_dir, "minprefs.cfg"), os.path.join(user_dir, cfg))
    if "seed_from" in flow:
        saves = os.path.join(out, flow["seed_from"], "User Data", "MechCommander2", "savegame")
        if not os.path.isdir(saves):
            return ["needs the saves of flow %r (run it first)" % flow["seed_from"]]
        shutil.copytree(saves, os.path.join(user_dir, "savegame"))
    if "seed_dir" in flow:
        import glob
        dest = os.path.join(user_dir, "savegame")
        os.makedirs(dest, exist_ok=True)
        found = sorted(glob.glob(os.path.join(SAVES, flow["seed_dir"])))
        if not found:
            return ["no saves match tests/saves/%s" % flow["seed_dir"]]
        for src in found:
            shutil.copy(src, dest)
        if flow.get("foreign"):
            # another build's stamp (same length, so the packet table stays)
            path = os.path.join(dest, "testgame.ims")
            data = open(path, "rb").read()
            old = b"00.06.01.0614\0"
            if data.count(old) != 1:
                return ["%s has no single version stamp" % path]
            data = data.replace(old, b"00.06.01.0615\0")
            open(path, "wb").write(data)
    script = os.path.join(work, "input.txt")
    with open(script, "w") as f:
        f.write("\n".join(flow["script"]) + "\n")
    frames = flow["frames"]
    # a capture every 100 frames shows where a flow went wrong
    captures = list(range(100, frames + 1, 100))
    env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER="dummy", MC2_NO_DIALOGS="1",
               MC2_NO_MOVIES="1", MC2_USER_DIR=user_dir)
    cmd = ([os.path.join(game_dir, "mc2")] + flow["args"] + FIXED_ARGS +
           ["-renderer", renderer, "-input-script", script, "-exit-after", str(frames),
            "-capture-frames", ",".join(map(str, captures)), "-capture-dir", work])
    log_path = os.path.join(work, "game.log")
    with open(log_path, "w") as log:
        try:
            status = subprocess.call(cmd, cwd=game_dir, env=env, stdout=log,
                                     stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired:
            status = "timeout (the game hung)"
    text = open(log_path, errors="replace").read()
    problems = []
    if status != 0:
        problems.append("exit status %s" % status)
    last = os.path.exists(os.path.join(work, "capture_%06d.png" % frames))
    if flow.get("quits"):
        if last:
            problems.append("still running at frame %d (should have quit)" % frames)
    elif not last:
        problems.append("no capture of frame %d" % frames)
    problems += ["log lacks %r" % e for e in flow.get("expect", []) if e not in text]
    problems += ["log has %r" % b for b in FORBIDDEN if b in text]
    problems += ["log has %r" % b for b in flow.get("forbid", []) if b in text]
    problems += ["no %s" % f for f in flow.get("expect_files", [])
                 if not os.path.exists(os.path.join(user_dir, f))]
    if "expect_cfg" in flow:
        problems += check_cfg(os.path.join(user_dir, "options.cfg"), flow["expect_cfg"])
    return problems


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
    unknown = [n for n in names if n not in FLOWS]
    if unknown:
        sys.exit("unknown flows: %s (known: %s)" % (", ".join(unknown), ", ".join(FLOWS)))
    xvfb, display = start_xvfb("800x600")
    failed = []
    try:
        for name in names:
            problems = run_flow(name, FLOWS[name], game_dir, out, args.renderer, display,
                                args.timeout)
            print("%-13s %s" % (name, "ok" if not problems else "FAIL: " + "; ".join(problems)),
                  flush=True)
            if problems:
                failed.append(name)
    finally:
        xvfb.kill()
    print("logs and captures in", out)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
