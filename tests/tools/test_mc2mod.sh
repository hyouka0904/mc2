#!/usr/bin/env bash
# End-to-end test of the mc2mod tool on a scratch directory and on the
# example mods shipped in examples/.
#   test_mc2mod.sh <mc2mod binary> <repo dir>
set -u
tool=${1:?usage: test_mc2mod.sh <mc2mod> <repo_dir>}
repo=${2:?}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# modding workflow: scaffold, add content, validate, resolve
"$tool" new "$work/mods" my_mod "My Mod" >/dev/null || fail "new"
[ -f "$work/mods/my_mod/mod.ini" ] || fail "mod.ini not created"
"$tool" new "$work/mods" my_mod >/dev/null && fail "new must refuse an existing mod"
"$tool" validate "$work/mods/my_mod" >/dev/null && fail "empty mod must not validate"
echo "pilot data" > "$work/mods/my_mod/content/data/objects/pilots.csv"
"$tool" validate "$work/mods/my_mod" | grep -q "ok: .*my_mod 0.1.0" || fail "validate"

"$tool" new "$work/mods" addon >/dev/null
sed -i 's/^\[dependencies\]$/[dependencies]\nmy_mod = ^0.1/' "$work/mods/addon/mod.ini"
echo "addon pilots" > "$work/mods/addon/content/data/objects/pilots.csv"
out=$("$tool" resolve "$work/mods") || fail "resolve: $out"
echo "$out" | grep -q "1. my_mod" || fail "dependency must load first: $out"
echo "$out" | grep -q "2. addon" || fail "dependent must load second: $out"
"$tool" which "$work/mods" "DATA/Objects/Pilots.csv" | grep -q "addon .*overrides my_mod" || fail "which"

"$tool" list "$work/mods" | grep -q "^addon" || fail "list"

# modpack with a missing mod is reported
"$tool" new-pack "$work/pack.ini" broken_pack my_mod ghost_mod=^1.0 >/dev/null || fail "new-pack"
"$tool" resolve "$work/mods" --pack "$work/pack.ini" | grep -q "'ghost_mod' is not installed" || fail "missing mod not reported"
"$tool" resolve "$work/mods" --pack "$work/pack.ini" >/dev/null && fail "broken pack must exit 1"

# shipped examples
"$tool" validate "$repo"/examples/mods/* >/dev/null || fail "example mods invalid"
"$tool" which "$repo/examples/mods" data/art/mcl_mm_1.tga | grep -q ": blue_main_menu" || fail "examples: blue should win"
"$tool" which "$repo/examples/mods" data/art/mcl_mm_1.tga --pack "$repo/examples/modpacks/red_menu_pack/modpack.ini" \
    | grep -q ": red_main_menu" || fail "examples: modpack should make red win"

"$tool" bogus >/dev/null 2>&1; [ $? -eq 2 ] || fail "unknown command must exit 2"
echo "PASS: mc2mod"
