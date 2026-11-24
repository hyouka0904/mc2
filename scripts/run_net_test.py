#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LAN multiplayer end to end: two game instances on this machine.

    run_net_test.py <game_dir> [--renderer gl|gpu] [--out DIR] [--timeout S]
                    [--scenario battle|version]

Both instances run headless (Xvfb, fixed time step, scripted mouse and
keys), each with its own user data directory, and talk over 127.0.0.1:

  host    main menu -> Multiplayer -> Local network -> Host game "ROOM",
          ticks Quickstart, launches once the client is ready
  client  main menu -> Multiplayer -> Local network -> LAN browser, picks
          the room, joins, ticks Ready

Both load WetWork (mc2_m01) with the quickstart forces. The host gets
MC2_NET_TEST_CHARGE=1, so after 10 seconds its units attack the client's.
Both write MC2_NET_STATEFILE (unit positions and states every second of the
host's mission clock). The client quits mid battle; the host carries on.

Checks: the client found and joined the room, both machines passed every
step of the loading handshake, there was a fight (units destroyed, kills
counted), both machines agree on which units are destroyed and on the
kill/loss tallies, live units are within a couple of map cells of each
other, and neither game logged a STOP, assertion or crash.

--scenario version: the host pretends to be release 9.9.9 with another
network protocol (MC2_NET_TEST_VERSION / MC2_NET_TEST_PROTOCOL). The client
must list the room as incompatible, show the version dialog when Join is
pressed and never connect. Captures: client/capture_001380.png (room list)
and client/capture_001560.png (dialog).
"""

import argparse
import math
import re
import os
import shutil
import subprocess
import sys
import tempfile
import time
from collections import defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "scripts"))
from run_screenshot_tests import FIXED_ARGS, start_xvfb  # noqa: E402
from run_flow_tests import FORBIDDEN, click  # noqa: E402

MP_BUTTON = (402, 264)        # main menu: Multiplayer
LAN = (406, 243)              # connection type: Local network
LAN_BROWSER = (597, 398)      # LAN panel: Go to LAN browser
HOST_GAME = (597, 447)        # LAN panel: Host game
HOST_NAME_EDIT = (297, 219)   # host dialog
HOST_ACCEPT = (550, 437)
QUICKSTART = (533, 490)       # parameters screen
LAUNCH = (722, 566)           # parameters screen Next / Launch, browser Join
FIRST_ROOM = (200, 116)       # LAN browser list
CLIENT_READY = (458, 145)     # second player row: Ready

HOST_FRAMES = 15000
CLIENT_FRAMES = 9500


def host_script():
    s = click(600, *MP_BUTTON) + click(800, *LAN) + click(900, *HOST_GAME)
    s += click(1000, *HOST_NAME_EDIT)
    s += ["1010 key R", "1014 key O", "1018 key O", "1022 key M"]
    s += click(1050, *HOST_ACCEPT) + click(1300, *QUICKSTART) + click(4600, *LAUNCH)
    return s


def client_script():
    return (click(600, *MP_BUTTON) + click(800, *LAN) + click(900, *LAN_BROWSER) +
            click(1400, *FIRST_ROOM) + click(1450, *LAUNCH) + click(2200, *CLIENT_READY))


def version_client_script():
    return (click(600, *MP_BUTTON) + click(800, *LAN) + click(900, *LAN_BROWSER) +
            click(1400, *FIRST_ROOM) + click(1450, *LAUNCH))


def start(name, script, frames, game_dir, out, renderer, display, extra_env, captures=None):
    work = os.path.join(out, name)
    shutil.rmtree(work, ignore_errors=True)
    user_dir = os.path.join(work, "User Data", "MechCommander2")
    os.makedirs(user_dir)
    for cfg in ("options.cfg", "orgprefs.cfg"):
        shutil.copy(os.path.join(game_dir, "minprefs.cfg"), os.path.join(user_dir, cfg))
    script_path = os.path.join(work, "input.txt")
    with open(script_path, "w") as f:
        f.write("\n".join(script) + "\n")
    env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER="dummy", MC2_NO_DIALOGS="1",
               MC2_NO_MOVIES="1", MC2_USER_DIR=user_dir,
               MC2_NET_STATEFILE=os.path.join(work, "state.txt"), **extra_env)
    cmd = ([os.path.join(game_dir, "mc2")] + FIXED_ARGS +
           ["-renderer", renderer, "-input-script", script_path, "-exit-after", str(frames),
            "-capture-frames", captures or "%d" % (frames - 10), "-capture-dir", work])
    log = open(os.path.join(work, "game.log"), "w")
    proc = subprocess.Popen(cmd, cwd=game_dir, env=env, stdout=log, stderr=subprocess.STDOUT)
    return proc, work


def load_state(path):
    units = defaultdict(dict)
    kills = {}
    if not os.path.exists(path):
        return units, kills
    for line in open(path):
        f = line.split()
        if len(f) < 2:
            continue
        if f[1] == "kills":
            kills[int(f[0])] = f[2:]
        elif f[1] != "dbg" and len(f) >= 6:
            units[int(f[0])][int(f[1])] = (float(f[2]), float(f[3]), f[4] == "1")
    return units, kills


def check(host_dir, client_dir, host_status, client_status):
    problems = []
    host_log = open(os.path.join(host_dir, "game.log"), errors="replace").read()
    client_log = open(os.path.join(client_dir, "game.log"), errors="replace").read()
    if host_status != 0:
        problems.append("host exit status %s" % host_status)
    if client_status != 0:
        problems.append("client exit status %s" % client_status)
    for what, text in (("host", host_log), ("client", client_log)):
        problems += ["%s log has %r" % (what, b) for b in FORBIDDEN if b in text]
        for step, cids in ((0, (0,)), (2, (0, 1)), (4, (0, 1))):  # step 0 comes from the host only
            for cid in cids:
                line = "[net] mission setup %d from commander %d" % (step, cid)
                if line not in text:
                    problems.append("%s log lacks %r" % (what, line))
    for expect in ("[net] found room \"room\"", "[net] joined as commander 1"):
        if expect not in client_log:
            problems.append("client log lacks %r" % expect)
    if "joined as commander 1" not in host_log:
        problems.append("host log lacks the join")

    h_units, h_kills = load_state(os.path.join(host_dir, "state.txt"))
    c_units, c_kills = load_state(os.path.join(client_dir, "state.txt"))
    common = sorted(set(h_units) & set(c_units))
    if len(common) < 30:
        problems.append("only %d seconds of mission on both machines" % len(common))
        return problems
    last = common[-1]
    destroyed = sum(1 for u in h_units[last].values() if u[2])
    if destroyed == 0:
        problems.append("no unit was destroyed (no fight?)")
    # destroyed state: allow a one-second lag, but never for long
    lagging = 0
    for s in common:
        diff = [i for i in h_units[s] if i in c_units[s] and h_units[s][i][2] != c_units[s][i][2]]
        lagging = lagging + 1 if diff else 0
        if lagging > 2:
            problems.append("destroyed units differ for 3 seconds at %d: %s" % (s, diff))
            break
    # positions of live units
    worst = (0.0, None, None)
    for s in common:
        for i, (x, y, dead) in h_units[s].items():
            if dead or i not in c_units[s] or c_units[s][i][2]:
                continue
            d = math.hypot(x - c_units[s][i][0], y - c_units[s][i][1])
            if d > worst[0]:
                worst = (d, s, i)
    if worst[0] > 300.0:
        problems.append("unit %s is %.0f apart at second %s" % (worst[2], worst[0], worst[1]))
    # kills / losses
    k_common = sorted(set(h_kills) & set(c_kills))
    if k_common and h_kills[k_common[-1]] != c_kills[k_common[-1]]:
        problems.append("kills differ at %d: host %s client %s" %
                        (k_common[-1], h_kills[k_common[-1]], c_kills[k_common[-1]]))
    print("  %d seconds compared, %d units destroyed, max distance %.0f, kills %s" %
          (len(common), destroyed, worst[0], " ".join(h_kills.get(last, [])[:2])))
    return problems


def check_version(host_dir, client_dir, host_status, client_status):
    problems = []
    host_log = open(os.path.join(host_dir, "game.log"), errors="replace").read()
    client_log = open(os.path.join(client_dir, "game.log"), errors="replace").read()
    for what, status in (("host", host_status), ("client", client_status)):
        if status != 0:
            problems.append("%s exit status %s" % (what, status))
    for what, text in (("host", host_log), ("client", client_log)):
        problems += ["%s log has %r" % (what, b) for b in FORBIDDEN if b in text]
    for expect in ('[net] found room "room"', '[net] room "room" is not compatible (version 9.9.9'):
        if expect not in client_log:
            problems.append("client log lacks %r" % expect)
    if "[net] joining" in client_log:
        problems.append("client tried to join an incompatible room")
    if re.search(r"\[net\] peer \d+ connected", host_log):
        problems.append("a client connected to the host")
    for frame in (1380, 1560):
        if not os.path.exists(os.path.join(client_dir, "capture_%06d.png" % frame)):
            problems.append("client frame %d was not captured" % frame)
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    ap.add_argument("--renderer", default="gl")
    ap.add_argument("--out", default=None, help="logs, state files and captures (default: temporary)")
    ap.add_argument("--timeout", type=int, default=900, help="seconds for the whole test")
    ap.add_argument("--scenario", choices=("battle", "version"), default="battle")
    args = ap.parse_args()
    game_dir = os.path.abspath(args.game_dir)
    out = os.path.abspath(args.out) if args.out else tempfile.mkdtemp(prefix="mc2net-")
    os.makedirs(out, exist_ok=True)

    xvfb, display = start_xvfb("800x600")
    deadline = time.time() + args.timeout
    host = client = None
    try:
        version_test = args.scenario == "version"
        if version_test:
            # The host keeps its room open until the client is done (stopped below)
            host, host_dir = start("host", host_script(), HOST_FRAMES, game_dir, out, args.renderer, display,
                                   {"MC2_NET_TEST_VERSION": "9.9.9", "MC2_NET_TEST_PROTOCOL": "MC2NET-T"})
        else:
            host, host_dir = start("host", host_script(), HOST_FRAMES, game_dir, out, args.renderer,
                                   display, {"MC2_NET_TEST_CHARGE": "1"})
        host_log = os.path.join(host_dir, "game.log")
        while time.time() < deadline and host.poll() is None:
            if "[net] hosting" in open(host_log, errors="replace").read():
                break
            time.sleep(0.5)
        if version_test:
            client, client_dir = start("client", version_client_script(), 1600, game_dir, out,
                                       args.renderer, display, {}, captures="1380,1560")
        else:
            client, client_dir = start("client", client_script(), CLIENT_FRAMES, game_dir, out,
                                       args.renderer, display, {})
        statuses = {}
        if version_test:
            try:
                statuses["client"] = client.wait(timeout=max(1, deadline - time.time()))
            except subprocess.TimeoutExpired:
                client.kill()
                statuses["client"] = "timeout"
            statuses["host"] = host.poll() if host.poll() not in (None, 0) else 0
            host.kill()
        for name, proc in (("client", client), ("host", host)):
            if name in statuses:
                continue
            try:
                statuses[name] = proc.wait(timeout=max(1, deadline - time.time()))
            except subprocess.TimeoutExpired:
                proc.kill()
                statuses[name] = "timeout"
    finally:
        for proc in (host, client):
            if proc and proc.poll() is None:
                proc.kill()
        xvfb.kill()

    print("net test (%s, %s): logs in %s" % (args.scenario, args.renderer, out))
    checker = check_version if args.scenario == "version" else check
    problems = checker(host_dir, client_dir, statuses["host"], statuses["client"])
    for p in problems:
        print("  FAIL: " + p)
    print("PASS" if not problems else "FAILED")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
