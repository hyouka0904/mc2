#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LAN multiplayer end to end: two game instances on this machine.

    run_net_test.py <game_dir> [--renderer gl|gpu] [--out DIR] [--timeout S]
                    [--scenario battle|logistics|version|away]

Both instances run headless (Xvfb, fixed time step, scripted mouse and
keys), each with its own user data directory, and talk over 127.0.0.1:

  host    main menu -> Multiplayer -> Local network -> Host game "ROOM",
          prepares a mech and pilot, launches once the client is ready
  client  main menu -> Multiplayer -> Local network -> LAN browser, picks
          the room, joins and prepares a mech and pilot

Both load WetWork (mc2_m01) with their prepared mechs. The host gets
MC2_NET_TEST_CHARGE=1, so after 10 seconds its units attack the client's.
Both write MC2_NET_STATEFILE (unit positions and states every second of the
host's mission clock). Both instances must exit cleanly.

Checks: the client found and joined the room, both machines passed every
step of the loading handshake, there was a fight (units knocked out, kills
counted), both machines agree on destroyed and disabled units and on the
kill/loss tallies, live units are within a couple of map cells of each
other, and neither game logged a STOP, assertion or crash.

--scenario logistics: each player buys a mech, assigns a pilot and returns
to the room. The client reopens preparation, backs out, then completes it
again. The host must observe ready, unready and ready before launching.
Both games must load the two prepared mechs.

--scenario away: after preparing, the client steps away (Away), comes back
(I'm back), opens the System menu and closes it. The host tries to start
while the client is away. The host must see ready, away, ready, unready,
ready, and the game must only start after that.

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
LAN = (406, 150)              # connection type: Local network
LAN_BROWSER = (597, 398)      # LAN panel: Go to LAN browser
HOST_GAME = (597, 447)        # LAN panel: Host game
HOST_NAME_EDIT = (297, 219)   # host dialog
HOST_ACCEPT = (550, 437)
NEXT = (722, 566)             # browser Join and logistics Next
START_GAME = (643, 488)       # room: Start game
FIRST_ROOM = (200, 116)       # LAN browser list
PREPARE = (722, 566)          # room: Prepare
BACK = (65, 566)              # briefing: Back
AWAY = (330, 28)              # room: Away / I'm back
SYSTEM = (702, 28)            # room: System menu
SYSTEM_CLOSE = (402, 358)     # System menu: Close

HOST_FRAMES = 15000
CLIENT_FRAMES = 9500


def host_room_script():
    s = click(600, *MP_BUTTON) + click(800, *LAN) + click(900, *HOST_GAME)
    s += click(1000, *HOST_NAME_EDIT)
    s += ["1010 key R", "1014 key O", "1018 key O", "1022 key M"]
    return s + click(1050, *HOST_ACCEPT)


def join_room_script():
    return (click(600, *MP_BUTTON) + click(800, *LAN) + click(900, *LAN_BROWSER) +
            click(1400, *FIRST_ROOM) + click(1450, *NEXT))


def prepare_mech(frame):
    actions = (PREPARE, NEXT,                   # briefing -> mech bay
               (660, 278), (619, 104), (400, 78), NEXT,  # purchase a mech
               (625, 104), (437, 78), NEXT,             # add it to deployment
               (635, 105), (441, 78), NEXT)             # assign a pilot, return
    return [event for i, xy in enumerate(actions) for event in click(frame + 100 * i, *xy)]


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
            units[int(f[0])][int(f[1])] = (float(f[2]), float(f[3]), f[4] == "1", f[5] == "1")
    return units, kills


def check(host_dir, client_dir, host_status, client_status, logistics=False, away=False):
    problems = []
    host_log = open(os.path.join(host_dir, "game.log"), errors="replace").read()
    client_log = open(os.path.join(client_dir, "game.log"), errors="replace").read()
    if host_status != 0:
        problems.append("host exit status %s" % host_status)
    if client_status != 0:
        problems.append("client exit status %s" % client_status)
    for what, text in (("host", host_log), ("client", client_log)):
        problems += ["%s log has %r" % (what, b) for b in FORBIDDEN if b in text]
        steps = [(6, (0,)), (0, (0,)), (1, (0, 1)), (2, (0, 1)), (4, (0, 1))]
        for step, cids in steps:
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
    if not common or h_units[common[0]] != c_units[common[0]] or len(h_units[common[0]]) != 2:
        problems.append("both players must load the two prepared mechs")
    if away:
        updates = re.findall(r"player update from commander 1: ready ([01]) away ([01])", host_log)
        states = [r + a for r, a in updates]
        transitions = [st for i, st in enumerate(states) if i == 0 or st != states[i - 1]]
        # ready, away (not ready), ready, System menu (not ready), ready
        if "".join(t[0] + t[1] for t in transitions[-5:]) != "1001100010":
            problems.append("client states seen by the host: %s (expected ... 10 01 10 00 10)"
                            % " ".join(transitions))
        last_ready = host_log.rfind("player update from commander 1: ready 1 away 0")
        setup = host_log.find("[net] mission setup")
        if setup != -1 and setup < last_ready:
            problems.append("the game started before the client was back and ready")
        return problems
    if logistics:
        ready_updates = re.findall(r"player update from commander 1: ready ([01])", host_log)
        transitions = "".join(state for i, state in enumerate(ready_updates)
                              if i == 0 or state != ready_updates[i - 1])
        if not transitions.endswith("101"):
            problems.append("client must be ready, cancel readiness, then finish preparation again")
        return problems
    if len(common) < 30:
        problems.append("only %d seconds of mission on both machines" % len(common))
        return problems
    last = common[-1]
    knocked_out = sum(1 for u in h_units[last].values() if u[2] or u[3])
    if knocked_out == 0:
        problems.append("no unit was destroyed or disabled (no fight?)")
    # Allow a short lag in destroyed and disabled state.
    lagging = 0
    for s in common:
        diff = [i for i in h_units[s] if i in c_units[s] and h_units[s][i][2:] != c_units[s][i][2:]]
        lagging = lagging + 1 if diff else 0
        if lagging > 2:
            problems.append("destroyed or disabled units differ for 3 seconds at %d: %s" % (s, diff))
            break
    # positions of live units
    worst = (0.0, None, None)
    for s in common:
        for i, (x, y, destroyed, disabled) in h_units[s].items():
            if destroyed or disabled or i not in c_units[s] or any(c_units[s][i][2:]):
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
    print("  %d seconds compared, %d units knocked out, max distance %.0f, kills %s" %
          (len(common), knocked_out, worst[0], " ".join(h_kills.get(last, [])[:2])))
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
    ap.add_argument("--scenario", choices=("battle", "logistics", "version", "away"), default="battle")
    args = ap.parse_args()
    game_dir = os.path.abspath(args.game_dir)
    out = os.path.abspath(args.out) if args.out else tempfile.mkdtemp(prefix="mc2net-")
    os.makedirs(out, exist_ok=True)

    xvfb, display = start_xvfb("800x600")
    deadline = time.time() + args.timeout
    host = client = None
    try:
        version_test = args.scenario == "version"
        logistics = args.scenario == "logistics"
        away = args.scenario == "away"
        # Try starting before preparation, then give both players time to finish.
        start_frame = 7900 if logistics else 6700
        host_preparation = (host_room_script() + click(1500, *START_GAME) +
                            prepare_mech(1700) + click(start_frame, *START_GAME))
        if away:
            # the first Start falls while the client is away and must do nothing
            script = (host_room_script() + prepare_mech(1700) + click(5600, *START_GAME) +
                      click(9000, *START_GAME))
            host, host_dir = start("host", script, 12000, game_dir, out, args.renderer, display,
                                   {"MC2_NET_LOG": "2"}, captures="5500,8000")
        elif logistics:
            host, host_dir = start("host", host_preparation, 11500, game_dir, out, args.renderer, display,
                                   {"MC2_NET_LOG": "2"}, captures="1480,1770,2470,2770,2870,8500")
        elif version_test:
            # The host keeps its room open until the client is done (stopped below)
            host, host_dir = start("host", host_room_script(), HOST_FRAMES, game_dir, out, args.renderer, display,
                                   {"MC2_NET_TEST_VERSION": "9.9.9", "MC2_NET_TEST_PROTOCOL": "MC2NET-T"})
        else:
            host, host_dir = start("host", host_preparation, HOST_FRAMES, game_dir, out, args.renderer,
                                   display, {"MC2_NET_TEST_CHARGE": "1"})
        host_log = os.path.join(host_dir, "game.log")
        while time.time() < deadline and host.poll() is None:
            if "[net] hosting" in open(host_log, errors="replace").read():
                break
            time.sleep(0.5)
        if version_test:
            client, client_dir = start("client", join_room_script(), 1600, game_dir, out,
                                       args.renderer, display, {}, captures="1380,1560")
        elif away:
            script = (join_room_script() + prepare_mech(2200) +
                      click(3700, *AWAY) + click(6200, *AWAY) +
                      click(6600, *SYSTEM) + click(7000, *SYSTEM_CLOSE))
            client, client_dir = start("client", script, 11000, game_dir, out, args.renderer, display, {},
                                       captures="3600,3900,6400,6800")
        elif logistics:
            script = (join_room_script() + prepare_mech(2200) +
                      click(3850, *PREPARE) + click(4200, *BACK) +
                      click(4700, *PREPARE) + click(5000, *NEXT) +
                      click(5300, *NEXT) + click(5600, *NEXT))
            # The host starts at its frame 7900, about 6800 here; loading takes
            # another ~2500 frames
            client, client_dir = start("client", script, 10500, game_dir, out, args.renderer, display, {},
                                       captures="2180,3650,4490,5500,5900,7700")
        else:
            client, client_dir = start("client", join_room_script() + prepare_mech(2200), CLIENT_FRAMES, game_dir, out,
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
    if version_test:
        problems = check_version(host_dir, client_dir, statuses["host"], statuses["client"])
    else:
        problems = check(host_dir, client_dir, statuses["host"], statuses["client"], logistics, away)
    for p in problems:
        print("  FAIL: " + p)
    print("PASS" if not problems else "FAILED")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
