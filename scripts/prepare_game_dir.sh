#!/usr/bin/env bash
# Builds the game data from the mc2srcdata repository and assembles a
# runnable game directory.
#
#   scripts/prepare_game_dir.sh <build_dir> <mc2srcdata_dir> <game_dir>
#
#   build_dir       CMake build directory of this repository (already built)
#   mc2srcdata_dir  clone of https://github.com/alariq/mc2srcdata
#   game_dir        output directory (created if needed)
#
# Environment:
#   MC2_PLACEHOLDER_MOVIES=1  replace Git LFS pointer files in movies/ with
#                             1-second test clips (for CI / environments
#                             without LFS access; the game runs fine with them)
set -euo pipefail

if [ $# -ne 3 ]; then
    sed -n '2,15p' "$0"
    exit 2
fi

build_dir=$(cd "$1" && pwd)
data_dir=$(cd "$2" && pwd)
mkdir -p "$3"
game_dir=$(cd "$3" && pwd)   # absolute: the script changes directory below
repo_dir=$(cd "$(dirname "$0")/.." && pwd)

tools="$build_dir/out/data_tools"
for t in aseconv makefst makersp pak; do
    [ -x "$tools/$t" ] || { echo "missing $tools/$t - build the project first" >&2; exit 1; }
done
[ -x "$build_dir/out/text_tool/text_tool" ] || { echo "missing text_tool" >&2; exit 1; }
[ -x "$build_dir/mc2" ] || { echo "missing $build_dir/mc2" >&2; exit 1; }

# Movies are stored with Git LFS. Without LFS the files are small text
# pointers that ffmpeg cannot read.
lfs_pointers=()
while IFS= read -r -d '' f; do
    if head -c 40 "$f" | grep -q 'git-lfs.github.com/spec'; then
        lfs_pointers+=("$f")
    fi
done < <(find "$data_dir/movies" -name '*.mp4' -print0 2>/dev/null)

if [ ${#lfs_pointers[@]} -gt 0 ]; then
    if [ "${MC2_PLACEHOLDER_MOVIES:-0}" = 1 ]; then
        echo "Replacing ${#lfs_pointers[@]} LFS pointer movies with placeholder clips"
        for f in "${lfs_pointers[@]}"; do
            ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=15 \
                -t 1 -pix_fmt yuv420p "$f"
        done
    else
        echo "error: ${#lfs_pointers[@]} movies are Git LFS pointers." >&2
        echo "Run 'git lfs pull' in $data_dir, or set MC2_PLACEHOLDER_MOVIES=1." >&2
        exit 1
    fi
fi

echo "Building game data in $data_dir/build_scripts"
cp "$tools"/{aseconv,makefst,makersp,pak} "$build_dir/out/text_tool/text_tool" "$data_dir/build_scripts/"
make -C "$data_dir/build_scripts" -j"$(nproc)" all BUILD_PLATFORM=linux

echo "Assembling $game_dir"
cd "$data_dir/build_scripts"
cp -r data assets ./*.cfg ./*.fst testtxm.tga "$game_dir/"
cp "$build_dir/mc2" "$game_dir/"
cp "$build_dir/out/res/libmc2res_64.so" "$game_dir/libmc2res.so"
cp -r "$repo_dir/shaders" "$game_dir/"

echo "Done. Run the game with:  cd $game_dir && ./mc2"
