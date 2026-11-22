#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Round-trip tests for the data tools used to build the game's data packs.
#
#   test_data_tools.sh <case> <tools_dir>
#
# <case> is one of: fastfile, pak, rsp, text_tool. Exits 0 on success,
# 77 when a prerequisite is missing (reported by CTest as "skipped").
set -euo pipefail

case_name=$1
tools=$2

work=$(mktemp -d "${TMPDIR:-/tmp}/mc2_tooltest.XXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"

fail() { echo "FAIL: $*" >&2; exit 1; }

# A small tree with a text file, a binary file, an empty file and a subdir.
make_fixture() {
    mkdir -p src/sub
    printf 'hello mech commander\n' > src/a.txt
    head -c 70000 /dev/urandom > src/sub/b.bin
    : > src/empty.dat
}

case "$case_name" in
fastfile)
    make_fixture
    "$tools/makefst" -f test.fst -p src/ -m data/test > pack.log 2>&1 || { cat pack.log; fail "makefst pack"; }
    [ -s test.fst ] || fail "no fastfile written"
    mkdir out
    "$tools/makefst" -d -f test.fst -p out > unpack.log 2>&1 || { cat unpack.log; fail "makefst unpack"; }
    diff -r src out/test.fst/data/test || fail "unpacked tree differs from source"
    ;;

rsp)
    make_fixture
    "$tools/makersp" -rsp src/list.rsp -p src/ -w "*.txt" -w "sub/*.bin" > rsp.log 2>&1 || { cat rsp.log; fail "makersp"; }
    grep -q 'a.txt$' src/list.rsp || fail "a.txt missing from rsp"
    grep -q '^sub/b.bin$' src/list.rsp || fail "sub/b.bin missing from rsp"
    [ "$(wc -l < src/list.rsp)" -eq 2 ] || fail "rsp should list exactly 2 files"
    ;;

pak)
    make_fixture
    printf 'a.txt\nsub/b.bin\nempty.dat\nmissing.txt\n' > src/list.rsp
    for mode in raw compressed; do
        flags=""; [ "$mode" = compressed ] && flags="-c"
        # shellcheck disable=SC2086
        "$tools/pak" $flags -f $mode.pak -r src/list.rsp > pack_$mode.log 2>&1 || { cat pack_$mode.log; fail "pak $mode"; }
        mkdir out_$mode
        "$tools/pak" -d -f $mode.pak -p out_$mode > unpack_$mode.log 2>&1 || { cat unpack_$mode.log; fail "unpak $mode"; }
        cmp out_$mode/packet_0 src/a.txt || fail "$mode: packet 0"
        cmp out_$mode/packet_1 src/sub/b.bin || fail "$mode: packet 1"
        # empty and missing files become empty/NULL packets
        [ ! -s out_$mode/packet_2 ] || fail "$mode: packet 2 should be empty"
        [ ! -s out_$mode/packet_3 ] || fail "$mode: packet 3 should be empty"
    done
    # random data does not compress, but text-only packs should shrink
    [ "$(stat -c %s compressed.pak)" -le "$(( $(stat -c %s raw.pak) + 64 ))" ] || fail "compression grew the pack"
    ;;

text_tool)
    font=$(fc-match -f '%{file}' 'DejaVu Sans' 2>/dev/null || true)
    if [ -z "$font" ] || [ ! -f "$font" ]; then
        echo "no TrueType font found (install fontconfig + fonts-dejavu); skipping"
        exit 77
    fi
    # Must work without any display: the tool only rasterizes with SDL_ttf.
    env -u DISPLAY -u WAYLAND_DISPLAY "$tools/text_tool" "$font" 12 out_font > tt.log 2>&1 \
        || { cat tt.log; fail "text_tool"; }
    [ -s out_font.bmp ] || fail "no glyph atlas written"
    [ -s out_font.glyph ] || fail "no glyph metrics written"
    [ "$(head -c 2 out_font.bmp)" = "BM" ] || fail "atlas is not a BMP"
    ;;

*)
    fail "unknown case $case_name"
    ;;
esac

echo "PASS: $case_name"
