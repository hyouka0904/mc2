#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# End-to-end smoke test: launches the real game headless and checks that it
# reaches gameplay without crashing or getting stuck on a dialog.
#
#   scripts/smoke_test.sh <game_dir> [mission] [seconds]
#
#   game_dir  directory prepared by scripts/prepare_game_dir.sh
#   mission   mission to start directly (default: mc2_01)
#   seconds   how long the game must keep running (default: 60)
#
# Runs under Xvfb with Mesa software OpenGL, so no GPU or display is needed.
# Writes smoke.log and smoke.png into $MC2_SMOKE_OUT (default: game_dir).
# Extra game arguments can be given in $MC2_SMOKE_ARGS (e.g. "-renderer gpu").
#
# The test fails if the game exits before the time is up (crash or quit),
# shows any message box, logs an OpenGL error, or renders an all-black frame.
set -uo pipefail
# message boxes must not wait for a click (the test fails on them anyway)
export MC2_NO_DIALOGS=1

game_dir=${1:?usage: smoke_test.sh <game_dir> [mission] [seconds]}
mission=${2:-mc2_01}
seconds=${3:-60}
out_dir=${MC2_SMOKE_OUT:-$game_dir}
mkdir -p "$out_dir"
# absolute paths: the script changes into the game directory below
game_dir=$(cd "$game_dir" && pwd)
out_dir=$(cd "$out_dir" && pwd)
log="$out_dir/smoke.log"
shot="$out_dir/smoke.png"

for tool in Xvfb xdpyinfo import convert; do
    command -v "$tool" >/dev/null || { echo "missing $tool (install xvfb x11-utils imagemagick)"; exit 2; }
done

cd "$game_dir"

display=:$(( 90 + RANDOM % 100 ))
Xvfb "$display" -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
xvfb_pid=$!
cleanup() { kill "$xvfb_pid" 2>/dev/null || true; }
trap cleanup EXIT
for _ in $(seq 50); do xdpyinfo -display "$display" >/dev/null 2>&1 && break; sleep 0.1; done

export DISPLAY=$display SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1

# The very first launch runs a hardware check and exits by design; do it
# once up front so the timed run below starts the actual game.
if [ ! -f options.cfg ]; then
    echo "First launch: running the one-time hardware check"
    timeout 120 ./mc2 > "$out_dir/first_run.log" 2>&1 || true
    [ -f options.cfg ] || { echo "FAIL: first run did not create options.cfg"; tail -20 "$out_dir/first_run.log"; exit 1; }
fi

echo "Running mission $mission for ${seconds}s on $display"
# shellcheck disable=SC2086 # word splitting of the extra arguments is intended
./mc2 -mission "$mission" ${MC2_SMOKE_ARGS:-} > "$log" 2>&1 &
game_pid=$!

sleep "$seconds"
if ! kill -0 "$game_pid" 2>/dev/null; then
    wait "$game_pid"; status=$?
    echo "FAIL: game exited early with status $status"
    grep -v shader_builder "$log" | tail -30
    exit 1
fi

import -display "$display" -window root "$shot"
kill -TERM "$game_pid" 2>/dev/null
for _ in $(seq 50); do kill -0 "$game_pid" 2>/dev/null || break; sleep 0.2; done
if kill -0 "$game_pid" 2>/dev/null; then
    echo "FAIL: game ignored SIGTERM"
    kill -KILL "$game_pid"
    exit 1
fi

failed=0
if grep -q '^MSGBOX:' "$log"; then
    echo "FAIL: game showed a message box:"; grep '^MSGBOX:' "$log" | head -5; failed=1
fi
if grep -qE 'OpenGL Error|Segmentation|Aborted|ASSERT:|VERIFY:' "$log"; then
    echo "FAIL: errors in log:"; grep -E 'OpenGL Error|Segmentation|Aborted|ASSERT:|VERIFY:' "$log" | head -5; failed=1
fi

# Mean brightness 0..1; a black frame means nothing was rendered.
brightness=$(convert "$shot" -colorspace Gray -format '%[fx:mean]' info:)
if awk "BEGIN { exit !($brightness < 0.02) }"; then
    echo "FAIL: screenshot is black (mean brightness $brightness)"; failed=1
fi

if [ $failed -eq 0 ]; then
    echo "PASS: mission $mission ran ${seconds}s, brightness $brightness, screenshot $shot"
fi
exit $failed
