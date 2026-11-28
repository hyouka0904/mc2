# Development Guide

[中文版](DEVELOPMENT.zh-TW.md) ｜ [Back to README](../README.md)

For people who want to build the game, change the code or contribute. To just play, download a package from the [README](../README.md).

## Contents

1. [Architecture](#architecture)
2. [Building on Linux](#building-on-linux)
3. [Building the game data and running](#building-the-game-data-and-running)
4. [How LAN multiplayer works](#how-lan-multiplayer-works)
5. [Update check](#update-check) / [Save game compatibility](#save-game-compatibility)
6. [Automated tests](#automated-tests)
7. [Building on Windows](#building-on-windows) / [Building on macOS](#building-on-macos)
8. [Packages and releases](#packages-and-releases)
9. [Known limitations and open items](#known-limitations-and-open-items)

- Mod format, tool and API: [doc/modding.md](modding.md)
- Roadmap for the multiplayer modes (co-op, roguelike, 1v1, ML interface) and the Chinese translation: [ROADMAP_MULTIPLAYER_ZH.md](ROADMAP_MULTIPLAYER_ZH.md) (Chinese only)

---

## Architecture

### Layers

The code has four layers; each depends only on the ones below it:

```
┌──────────────────────────────────────────────────────────────┐
│ code/      game logic: missions, 'Mechs and vehicles, AI,    │
│            combat, logistics screens, UI                     │
├──────────────────────────────────────────────────────────────┤
│ gui/       UI widgets (buttons, lists, fonts, animations)    │
│ mclib/     engine: terrain, objects, pathfinding, ABL        │
│            scripts, texture management                       │
│   ├ mlr/     render pipeline (MLR, 3D models, vertex pools)  │
│   ├ gosfx/   effects (particles, cards, shape effects)       │
│   └ stuff/   math and containers (vectors, matrices,         │
│              quaternions, linked lists, streams)             │
├──────────────────────────────────────────────────────────────┤
│ GameOS/    platform layer (the reimplemented part)           │
│   ├ include/   original GameOS interface + Win32 types       │
│   ├ gameos/    window, input, audio, files, fonts on         │
│   │            SDL3 / SDL3_mixer                              │
│   │   └ render/  backend interface (Device / Shader /        │
│   │       │      PipelineState)                               │
│   │       ├ gl/  OpenGL (the only place that calls OpenGL)   │
│   │       └ gpu/ SDL3 GPU (Vulkan / D3D12 / Metal)            │
│   │            utils/ images, matrices, screenshots, input   │
│   │            scripts                                        │
│   └ src/       replacements for Win32 APIs (strings, files,  │
│                message boxes, time…)                          │
├──────────────────────────────────────────────────────────────┤
│ third party: SDL3 (with mixer, ttf), GLEW, zlib, pl_mpeg,    │
│ ENet                                                          │
└──────────────────────────────────────────────────────────────┘
```

### Directories

| Directory / file | Description |
|---|---|
| `code/` | The game itself (about 200 files). The entry point is `mechcmd2.cpp`: initialization, command line and main loop. |
| `mclib/` | Engine core, e.g. `move.cpp` (A* pathfinding), `pqueue.cpp` (its priority queue), `txmmgr.cpp` (texture and draw batch management), `abl*.cpp` (ABL mission script interpreter), `ffile.cpp` / `packet.cpp` (`.fst` / `.pak` archive formats). |
| `gui/` | UI widget library. |
| `GameOS/` | Platform layer, see the diagram above. |
| `shaders/` | GLSL shaders. The OpenGL backend loads them from the game directory's `shaders/` at run time; the build also compiles them to SPIR-V / MSL / HLSL (see [Shaders](#shaders)). |
| `res/` | String resource library `libmc2res_64.so`, generated from `test_scripts/res_conv/strings.res.cpp`. |
| `data_tools/` | Data build tools: `aseconv` (ASE models to TGL), `makefst` (pack / unpack `.fst`), `makersp` (file lists), `pak` (pack / unpack `.pak`). |
| `text_tool/` | Font baking tool: turns TTF fonts into the game's font atlases (`.bmp`) and glyph data (`.glyph`). Needs no display. |
| `GameOS/gameos/mods/` | Mod system (`mod_system.h`: versions, manifests, modpacks, load order, file overlay; `gos_mods.h`: game hooks). |
| `tools/mc2mod/` | Mod tool `mc2mod` (create, validate, list, resolve load order). |
| `examples/mods/`, `examples/modpacks/` | Example mods and modpacks (also used by the tests). |
| `thirdparty/enet/` | ENet 1.3.18 (MIT), reliable UDP for LAN multiplayer. The game uses it only through `GameOS/gameos/net/gos_net.h` (connections, reliable / unreliable channels, answering room discovery queries on game port 27015). |
| `thirdparty/pl_mpeg/` | Single-file MPEG-1 video / MP2 audio decoder (MIT) for cutscenes. `code/mc2movie.cpp` converts each frame to RGBA, updates a texture and draws it as a quad, so every render backend can show it; movie audio goes to SDL3_mixer through `gosAudio_*PCMStream`. If `data/sound/` has a `.wav` with the same name, that file plays instead. |
| `tests/` | Automated tests (see [Automated tests](#automated-tests)). |
| `scripts/` | `prepare_game_dir.sh` (build the game data and assemble a runnable game directory), `smoke_test.sh` (headless end-to-end test), and the test, packaging and benchmark scripts. |
| `cmake/MC2Dependencies.cmake` | Gets SDL3 / SDL3_mixer / SDL3_ttf: an installed version if there is one, otherwise a pinned version downloaded and built statically. |
| `.github/workflows/ci.yml` | GitHub Actions: build, unit tests, smoke test that runs the game. |
| `test_scripts/res_conv/` | UI string sources (`strings.res.cpp`, `resource.h`) and the upstream conversion script `res_conv.pl`. |
| `doc/` | Player manuals and the modding guide. |

### Game data pipeline

The game data is not in this repository but in [mc2srcdata](https://github.com/alariq/mc2srcdata). The build goes:

```
mc2srcdata (source assets)
   │  aseconv / makefst / makersp / pak / text_tool / ffmpeg
   ▼
*.fst, *.pak, data/, assets/ (packed game data)
   │  scripts/prepare_game_dir.sh
   ▼
game directory: mc2 + libmc2res.so + shaders/ + game data
```

At run time `File::open()` looks for a loose file in the game directory first and falls back to the `.fst` archives.

---

## Building on Linux

Verified on **Ubuntu 24.04 (GCC 13, CMake 3.28)**.

### 1. Install dependencies

```bash
sudo apt-get install build-essential cmake git libglew-dev libgl-dev zlib1g-dev libfreetype-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev libxfixes-dev libxkbcommon-dev libpulse-dev libasound2-dev
```

Ubuntu 24.04 has no SDL3 packages, so on the first configure CMake downloads and statically builds pinned versions of
SDL 3.4.8, SDL3_mixer 3.2.4 (only WAVE and other formats that need no external libraries) and SDL3_ttf 3.2.2 (with the system FreeType);
the resulting binary depends on no SDL shared library. The X11 / PulseAudio / ALSA development packages above let SDL3 build those backends.
If SDL3 is installed (e.g. in `/opt/sdl3`), pass `-DCMAKE_PREFIX_PATH=/opt/sdl3` to use it.

### 2. Build

```bash
cmake -S . -B build
```

```bash
cmake --build build -j"$(nproc)"
```

Without `CMAKE_BUILD_TYPE` the default is `RelWithDebInfo` (optimized, with debug info).
Other options:

| Option | Description |
|---|---|
| `-DCMAKE_BUILD_TYPE=Debug` | Debug build with `gosASSERT` and other assertions |
| `-DMC2_BUILD_TESTS=OFF` | Do not build the tests |
| `-DMC2_ENABLE_ASAN=ON` | Build with AddressSanitizer |
| `-DMC2_SDL3_SOURCE=AUTO\|SYSTEM\|FETCH` | Where SDL3 comes from: `AUTO` (default: installed if present, otherwise downloaded), `SYSTEM` (installed only), `FETCH` (always downloaded) |

### 3. Outputs

| File | Purpose |
|---|---|
| `build/mc2` | Game executable |
| `build/out/res/libmc2res_64.so` | String resources (rename to `libmc2res.so` in the game directory) |
| `build/out/data_tools/{aseconv,makefst,makersp,pak}` | Data build tools |
| `build/out/text_tool/text_tool` | Font baking tool |

---

## Building the game data and running

### 1. Get the game data

```bash
git clone https://github.com/alariq/mc2srcdata.git
```

The cutscenes are stored with Git LFS. With `git-lfs` installed, run after cloning:

```bash
git -C mc2srcdata lfs pull
```

You also need `ffmpeg`: the data build converts the movies to MPEG-1 (`.mpg`), which the game decodes with the bundled [pl_mpeg](../thirdparty/pl_mpeg/pl_mpeg.h) (MIT).

```bash
sudo apt-get install ffmpeg
```

### 2. Build the data and assemble the game directory

```bash
scripts/prepare_game_dir.sh build mc2srcdata game
```

The script copies the data tools into `mc2srcdata/build_scripts`, runs the data build (about 15 seconds),
then assembles the data, the executable, `libmc2res.so` and `shaders/` into `game/`.

Without the LFS movies you can use 1-second test patterns instead (the game runs normally; the cutscenes are just test patterns):

```bash
MC2_PLACEHOLDER_MOVIES=1 scripts/prepare_game_dir.sh build mc2srcdata game
```

### 3. Run

```bash
cd game && ./mc2
```

- **On first start** the game uses the highest quality preset (`hiprefs.cfg`). The original ran a hardware check and quit; that now only happens with `-sniffer`.
- Version: `./mc2 -version` (also shown in the window title). CMake sets it from `git describe` at configure time; override with `-DMC2_VERSION=1.2.3`.
- Start a mission directly (skip the menus): `./mc2 -mission mc2_01`
- Settings (`options.cfg`, `orgprefs.cfg`) and saves live in the system application data folder (`SDL_GetPrefPath`), so the game directory can be read-only:
  `~/.local/share/MechCommander2/` (or `$XDG_DATA_HOME/MechCommander2/`) on Linux,
  `~/Library/Application Support/MechCommander2/` on macOS, `%APPDATA%\MechCommander2\` on Windows.
  `MC2_USER_DIR` overrides it (the tests use it to start from an empty folder).
- **Old save migration**: older versions used `~/.mechcommander2/` (or `$XDG_CONFIG_HOME/.mechcommander2/`, `%USERPROFILE%\.mechcommander2\` on Windows).
  When the new folder is empty, the game copies the old folder's contents over and leaves `MOVED.txt` in the old folder pointing to the new one; the old files are not deleted. Nothing is copied once the new folder has data.
  `options.cfg` / `orgprefs.cfg` that older versions wrote into the game directory are also copied over on first start.
- The game must run in the game directory (the one containing `data/`). When started from a file manager (Finder, Explorer),
  it looks for `data/` next to the executable, in `../share/mechcommander2/` relative to it (Linux packages), in a macOS `.app`'s `Contents/Resources/`, or next to the `.app`, and switches there.
  `-datapath <dir>` names the game directory directly. Relative paths in other options (e.g. `-mods`) are then relative to that directory.
- Errors show in a dialog and wait for an answer; with `-fixedstep`, an input script, or `MC2_NO_DIALOGS=1`
  they only go to stderr and are answered automatically (all test scripts set this).
- With `MC2_NO_MOVIES=1` every cutscene ends immediately. The screenshot tests and benchmarks set it so results do not depend on the movie files (the real LFS videos or test patterns of a different length).

---

## How LAN multiplayer works

For playing, see the [LAN skirmish manual](manual/lan-skirmish.md).

### Design

- **Host-authoritative**: the host runs the full simulation (AI, hit rolls, damage, mission outcome); clients only send orders and display the state the host sends. When a client drops, the host's AI takes over its units; when the host drops, the clients' mission ends.
- The transport is [ENet](../thirdparty/enet/) (reliable / unreliable UDP) behind `GameOS/gameos/net/gos_net.h`; game code never touches ENet directly.
- Star topology: clients connect only to the host, which relays. Messages are the `MCMSG_*` structs in `code/multplyr.h`, made only of fixed-size types, so the byte layout is identical on all three platforms.
- `code/multplyr.cpp`: rooms, roster, colors and teams, chat, load handshake. `code/multplyr_sync.cpp`: in-mission sync — unit movement / status chunks (sent on change, 10 Hz), weapon fire, critical hits, radio, hits, world events (mines, fire, artillery, building captures), orders, support units, pause, mission outcome.
- Room discovery shares UDP 27015 with the game: a scanner sends a query to 255.255.255.255, to every interface's subnet broadcast and to 127.0.0.1, and every host answers with its room info.

### Debug environment variables

| Variable | Effect |
|---|---|
| `MC2_NET_LOG=2` | Print every network message to stdout (default `1` prints only connection, join and load events; `0` turns it off) |
| `MC2_NET_STATEFILE=<file>` | Every second in a mission, write every unit's position and state plus kills / losses (clients use the host's mission clock), to compare two machines |
| `MC2_NET_TEST_CHARGE=1` | Test only: at 10 seconds the host orders its units to attack the nearest enemy |
| `MC2_NET_TEST_VERSION=<v>`, `MC2_NET_TEST_PROTOCOL=<stamp>` | Test only: pretend to be another release (game version in the room beacon, network protocol stamp) |

### Tests

- `unit_net` (CTest): host and client in one process; 1000 reliable packets each way (order and contents), 64 KB fragmentation, broadcast, room discovery (broadcast and direct IP), local interface enumeration (every address answers a direct IP query), timeout when nobody is listening.
- `scripts/run_net_test.py game`: two game instances on one machine, driven by script through main menu → host / join → prepare loadouts → Start game, into WetWork for a battle; compares destroyed and disabled units, kill counts and positions on both sides.
- `scripts/run_net_test.py game --scenario version`: the host pretends to be release 9.9.9 with another protocol; the client must show the room as "VERSION 9.9.9", show the version dialog on Join and never connect.

### Versions in the lobby

- The room beacon ends with the host's game version (hosts before 0.2 send the shorter beacon; it is still accepted and shown as "< 0.2"). The client marks a room compatible when the protocol stamp and the game data hash match its own.
- Incompatible rooms show `VERSION x.y.z` (or `DIFFERENT GAME DATA`) in the map column. Join does not connect; it shows both versions. If the host is newer the room list dialog offers **Download** (opens the release page); the direct IP panel says where to download it. If the host is older it says the host needs to update.
- The protocol stamp (`MC2NET-4` in `code/multplyr.cpp`) still decides whether two games can play together; the version string is only shown. Bump the stamp whenever a network message changes.

---

## Update check

`GameOS/gameos/update/gos_update.{h,cpp}` (check) and `gos_update_install.cpp` (download and install), started from `gameosmain.cpp` after the engine initialises, shown by `code/mainmenu.cpp`.

- A background thread asks `https://api.github.com/repos/hyouka0904/mc2/releases/latest` (5 s timeout). That endpoint only returns published releases, never drafts or pre-releases. The tag is compared with `MC2_VERSION` (SemVer, pre-releases sort first).
- The answer is kept in `update.ini` in the user directory; the network is asked **at most once every 24 hours**. The request carries only a generic `User-Agent`.
- HTTPS: WinHTTP on Windows; on Linux and macOS the system libcurl is loaded with `dlopen` (no build dependency; the AppImage uses the system's certificates). Without libcurl the check is skipped.
- No check for development builds: versions that do not parse, `0.0.0*` (local and CI test builds) and `git describe` versions past a tag (`1.2.0-14-gabc1234`).
- Main menu: a pulsing line in the bottom left, "NEW VERSION x.y.z AVAILABLE - CLICK HERE", only on the real main menu. The dialog offers **Download** (opens the GitHub release page; the notice comes back on the next start until the new version runs) and **Later** (hides the notice until the game is started again). Options > Gameplay has an update button under "Receive test versions" while that box is ticked or a newer version is known: check again, download and install (with the download size), or the download progress.
- Turned off by `-noupdatecheck`, `MC2_NO_UPDATE_CHECK=1` (set in CI), `check=0` in `update.ini`, and in deterministic or scripted runs (`-fixedstep`, `-input-script`).
- `MC2_UPDATE_URL=<url>` points the check at a test server (plain http allowed); with it development builds and scripted runs check too.
- Tests: `unit_update` (CTest) covers version parsing and ordering, the release JSON (drafts, pre-releases, malformed input, non-GitHub page URLs), `update.ini`, the notify rule, size formatting, and full background checks against a small HTTP server on 127.0.0.1 (caching, server errors, `check=0`, development builds).
- **Test channel**: Options > Gameplay > "Receive test versions" (`channel=test` in `update.ini`). The check then asks `releases?per_page=20` at every start (no 24-hour cache) and takes the newest release, pre-releases included; turning the option on or off checks again at once. Test builds come from running the Release workflow by hand (see [Publishing a release](#publishing-a-release)).
- **Download and install**: the dialog's **Update** downloads the package that matches the installation into `updates/` in the user directory (the notice shows the progress), checks it against the release's `SHA256SUMS.txt`, then asks to install and restart:

  | Installed as | Package | Install |
  |---|---|---|
  | Windows installer (`unins000.exe` next to `mc2.exe`) | `-windows-x64-setup.exe` | `/VERYSILENT /DIR=<game folder> /MC2RELAUNCH=1`; the game quits, the installer starts it again (`IsGameUpdate` in `mc2.iss`; `/ALLUSERS` and a UAC prompt when the folder is not writable) |
  | Windows zip | `-windows-x64.zip` | files replaced in place; the running `mc2.exe` and DLLs are renamed to `*.mc2old` first and removed at the next start |
  | Linux AppImage (`$APPIMAGE`) | `-linux-x86_64.AppImage` | written next to the old file and renamed over it |
  | Linux tarball (`<root>/usr/bin/mc2`) | `-linux-x86_64.tar.gz` | files replaced in place like the zip |
  | macOS | `-macos-arm64.dmg` | the disk image is opened; the player drags the app to Applications |

- **Part updates** (Windows installer and zip, Linux tarball): when the release has `<package>-manifest.json` for the installation, the game downloads only what changed instead of the whole package (about 600 MB):
  - `scripts/package.py` splits each package into parts: `-<platform>-program.zip` (executables, shaders, licenses, everything not in the game data) and `-data-<name>.zip` for the game data (`textures`, `models` = `tgl.fst`, `archives` = the other `.fst`, `sound`, `movies`, `misc`). The data parts are written once by the Linux job and shared by every platform (`--write-data-parts` / `--data-parts`; the Windows run checks that its data is the same). The manifest lists every part (asset name, size, SHA-256, folder below the install root) and every file in it (path, size, SHA-256, executable bit).
  - The game checks the manifest against `SHA256SUMS.txt`, hashes the installed files (sizes first; `updates/filehash.txt` remembers hashes by size and modification time), and downloads only the parts that hold a missing or different file, each checked against the manifest. Only the changed files of those parts are unpacked and checked again before anything is replaced, all or nothing like the full install. Skipping versions works: it compares with what is installed, not with the previous version.
  - A code-only change downloads the program part: about 5 MB on Linux (the executables lose their debug information when packaged, function names stay) and the size of `mc2.exe` plus DLLs on Windows. The data parts are the same in every release as long as the data does not change: the data build is byte-for-byte reproducible (`tgl.fst` used to differ in every build because `aseconv` wrote uninitialised bytes; `mclib/tgl.*`, `msl.cpp` now zero them).
  - A folder the game cannot write (Windows, all-users install in Program Files): the game starts `mc2.exe -apply-update <plan>` with administrator rights (one UAC prompt), waits for it and restarts. The helper writes the files, schedules the old ones for removal at the next restart of Windows and sets the version shown in "Installed apps".
  - Files that a part update adds are not in the installer's uninstall list, so uninstalling leaves them in the game folder.
  - AppImage and macOS keep downloading the whole package; releases without a manifest too.

  Every new file is written (`*.mc2new`) before anything is renamed, so a broken download changes nothing. Downloads are only taken from `https://github.com/hyouka0904/mc2/releases/download/` (or the `MC2_UPDATE_URL` test server). Anything else (other installs, a read-only folder, no package, a failure) falls back to opening the release page. A downloaded but not installed package is used again on the next start; older ones are removed.
- The checksum guards against broken downloads, not against a compromised GitHub account; signing (minisign, U4 in the roadmap) is still to do.
- Tests: `unit_update` also covers the release list (channels, drafts, asset names), SHA-256 test vectors, `SHA256SUMS.txt`, picking the package, finding a tarball install, unpacking zip and tar.gz (long names, symlinks, `..` entries, damaged archives that must change nothing) and downloads (errors and cancelling remove the file); for part updates the manifest (paths that leave the game folder, bad hashes), finding the changed files and the hash cache, applying parts (a wrong hash or a missing file changes nothing; executable bits) and the helper's plan file. The Release workflow applies a part update to the tarball and to an all-users Windows install in Program Files (`scripts/part_plan.py` writes the plan; the helper replaces its own running exe and the "Installed apps" version).

---

## Save game compatibility

Saves live in `savegame/` in the user directory. There are two kinds, and they age very differently.

| | Campaign save (`*.fit`, between missions) | In-mission quick save (`testgame.ims`) |
|---|---|---|
| Content | C-Bills, pilots, 'mech designs and inventory, campaign progress | The whole battlefield |
| Format | Text, `name = value` | Game structures (`MechData`, `MoverData`, ...) written byte for byte |
| Version | `VersionNumber` (`SaveGameVersionNumber` in `code/logisticsdata.cpp`) | `versionStamp` plus a layout fingerprint |
| Across versions | Must keep loading | May stop loading after a change to the structures; refused with a message |

**Campaign saves.** `load()` accepts every format from `OldestSaveGameVersionNumber` to `SaveGameVersionNumber` (both 10004 today); the Load Game list shows the same range. To change the format: raise `SaveGameVersionNumber`, keep reading the older number in `load()` and convert what changed, and leave `OldestSaveGameVersionNumber` alone. Things a save names that this version lacks no longer stop the load: a design whose chassis is missing, a component that does not exist or fit, and a 'mech built on a missing design are left out, each with a `[save]` line on stderr. A successful load prints `[save] campaign save loaded (format N): ...`.

**Quick saves.** The file starts with `00.06.01.0614 L<hash>`; the hash (`code/savelayout.cpp`) is FNV-1a over the sizes of every structure the quick save stores, and the loader also tells objects apart by packet size. A file with another hash is refused with "Cannot load Quick Save. Version does not match game" and the mission goes on, instead of loading garbage. Files from before the fingerprint (`00.06.01.0614` alone) load while the layout is still the one those builds had: the frozen values in `SaveLayoutIsLegacy()`. The fingerprint is per platform (Windows has 4-byte `long`), so a quick save does not move between Windows and Linux either.

When a saved structure changes size, `unit_savelayout` fails on purpose: update `kCurrent` in `tests/unit/test_savelayout.cpp` (never the legacy values), and say in the release notes that older quick saves no longer load.

**Old saves in the tests.** `tests/saves/<version>/` holds a campaign save and a Linux quick save of each release. The flows `old_campaign_save`, `old_quick_save` and `foreign_quick_save` load them in CI. Add a directory for each release whose save format changed.

Updates never touch the user directory, so saves and settings survive an update.

---

## Automated tests

### Unit and tool tests (CTest)

```bash
ctest --test-dir build --output-on-failure
```

| Test | What it covers |
|---|---|
| `unit_platform` | Cross-platform string functions (`S_strcmp` etc.) and Win32 replacements (`MessageBox` auto-answer) |
| `unit_kv_registry` | Windows registry emulation (read / write, format, edge cases) |
| `unit_image_tga` | TGA decoding: uncompressed and RLE, 24/32/16 bit, corrupt data |
| `unit_pqueue` | Pathfinding priority queue, including randomized tests against a reference model |
| `unit_run_options` | Deterministic-mode options, PNG screenshot encoding, fixed-step clock, input script parsing |
| `unit_sound` | Audio API (SDL3_mixer): play once / loop / pause / stop, linear volume, panning, freeing resources; 3D audio distance attenuation, listener-relative panning, Doppler, moving listener; output limiter (quiet sounds unchanged, loud sounds not clipped and recover, several loud sounds at once not clipped); needs no sound card |
| `unit_heap` | GameOS memory heaps: `gos_Malloc` / `new` are tracked per current heap, `gos_DestroyMemoryHeap(heap, false)` frees what is left, `(heap, true)` only reports leaks and the blocks stay usable |
| `unit_render_state` | Pipeline descriptions derived from GameOS render state, 32-bit pipeline keys (reversible, unique), pipeline cache |
| `unit_net` | LAN transport, see [How LAN multiplayer works](#how-lan-multiplayer-works) |
| `unit_update` | Update check, see [Update check](#update-check) |
| `unit_savelayout` | Sizes of the structures the quick save stores, see [Save game compatibility](#save-game-compatibility) |
| `render_isolation` | Checks that OpenGL appears only in `GameOS/gameos/render/gl/` |
| `shader_validation` | Compiles and links every shader variant offline with glslangValidator (same `#version 420`, `ALPHA_TEST` define and `#include` expansion as at run time), no GPU needed; skipped without `glslang-tools` |
| `shader_spirv` | Every variant compiles to Vulkan SPIR-V and passes `spirv-val`, the two stages' interfaces link, conversion to MSL / HLSL works, and the `PerDraw` uniform block offsets match the C++ struct; `-DMC2_SHADER_REQUIRE=msl,hlsl` makes the converters mandatory (CI uses it) |
| `unit_mods` / `tool_mc2mod` | Mod system and tool (see [doc/modding.md](modding.md)) |
| `unit_stuff` | Runs the self-tests built into the original Stuff math library that were never called |
| `tool_fastfile` / `tool_pak` / `tool_rsp` | Pack → unpack round trips for the data tools |
| `tool_text_tool` | Bakes a font without a display |

The tests use `tests/framework/mc2test.h`, a small framework with no external dependencies. To add a test:

```cpp
#include "mc2test.h"

TEST(my_feature_works)
{
    CHECK(1 + 1 == 2);
    CHECK_EQ(answer(), 42);
}
```

then register it in `tests/CMakeLists.txt` with `mc2_add_unit_test(...)`.

### End-to-end smoke test (runs the game)

Plays a mission under an Xvfb virtual display with Mesa software OpenGL; no GPU needed:

```bash
sudo apt-get install xvfb x11-utils imagemagick libgl1-mesa-dri
```

```bash
scripts/smoke_test.sh game mc2_01 60
```

The test uses a fresh user data folder (`MC2_USER_DIR`), so it also checks the first start: the game must go straight into the mission and write `options.cfg` to the user folder.
It fails if the game exits early (crash), shows a message box, reports an OpenGL error, ignores SIGTERM, writes no settings file, or the screen is black.
The screenshot is saved as `game/smoke.png`. Extra game options go in `MC2_SMOKE_ARGS`, e.g. for the SDL3 GPU backend:

```bash
MC2_SMOKE_ARGS="-renderer gpu" scripts/smoke_test.sh game mc2_01 60
```

Without a GPU, the SDL3 GPU backend uses Mesa's software Vulkan (lavapipe): `sudo apt-get install mesa-vulkan-drivers vulkan-validationlayers`.

### Screenshot comparison tests (deterministic mode)

A few GameOS-level command-line options make every run produce pixel-identical frames:

| Option | Effect |
|---|---|
| `-fixedstep <ms>` | Deterministic mode: the clock advances exactly `<ms>` per frame, `frameRate` is fixed at `1000/<ms>`, and the main loop does not sleep |
| `-seed <n>` | Fixed random seed, replacing every time-based seed (`gos_srand`, `srand(time)`, ABL `seedRandom(-1)`) |
| `-capture-frames a,b,c` | Read back the back buffer at those frames (counting from 1) and save PNGs |
| `-capture-dir <dir>` | Screenshot output folder |
| `-exit-after <n>` | Exit after frame `<n>` (default: the last capture frame) |
| `-input-script <file>` | Replay mouse / keyboard events from a script and ignore real input meanwhile (format in `GameOS/gameos/utils/input_script.h`) |
| `-perf-csv <file>` | One line per frame: frame number, wall time (ms), draw calls, pipeline state changes, distinct pipelines seen, main thread CPU time (ms) |
| `-datapath <dir>` | Game directory (the one containing `data/`) |
| `-version` | Print the version and exit |
| `-mods <dir>` / `-modpack <file>` | Enable mods / a modpack (see [doc/modding.md](modding.md)) |
| `-windowed` | Ignore the game's fullscreen setting and always use a window (fullscreen depends on the window manager and would make screenshots environment-dependent) |
| `-shader-reload` | Development: recompile shaders in game when their source changes (checked every half second, off by default) |
| `-renderer gl\|gpu` | Render backend: `gpu` = SDL3 GPU (default; Vulkan on Linux), `gl` = OpenGL. The screenshot script always passes it explicitly. `MC2_GPU_DEBUG=1` enables the Vulkan validation layers; `SDL_GPU_DRIVER` picks a driver |
| `-no-batching` | Disable draw batching: every `gos_Draw*` call draws on its own (for performance comparison or debugging) |
| `-gpu-shapes` | Transform and light the solid parts of 3D models on the GPU through `gos_RenderShape` (lighting matches the CPU path); slower on software rendering, off by default |

The tests run in an 800×600 Xvfb screen with `-windowed`, using a fresh user folder seeded with the low quality preset (`minprefs.cfg`, which the golden images were made with). `tests/golden/manifest.txt` lists 18 golden images: 3 frames each from 4 missions, the main menu, campaign selection, the Solo Mission screen,
a paused mission, and the main menu with the example mod / modpack. Run and compare:

```bash
sudo apt-get install python3-pil python3-numpy
```

```bash
scripts/run_screenshot_tests.py game shots
```

A pixel counts as "different" when any channel differs by more than 8/255; a test fails at 0.5% different pixels and writes a heat map to `shots/diff/`.
The golden images come from the OpenGL backend; the SDL3 GPU backend compares against the same set (`--renderer gpu`; `--gpu-validation` enables the Vulkan validation layers and fails on any validation message in the log).
Measured on lavapipe, the two backends differ by 0% on menus and UI and at most 0.25% on mission frames (rasterization differences at 3D model edges), so the threshold stays at 0.5%.
After an intentional visual change (e.g. a shader edit), regenerate the golden images with `--update` and commit them.
`scripts/compare_screenshots.py` compares two images or two folders on their own.

### UI flow tests (walk through every screen)

```bash
scripts/run_flow_tests.py game --renderer gl          # all flows
scripts/run_flow_tests.py game --only pause_menu      # only the named flow
```

Scripted mouse and keyboard input plays through every screen for real. Each flow has a timeout (300 seconds by default); running past it counts as a hang. The log must not contain STOP, FATAL, ASSERT, MSGBOX or Segmentation. The user folder path deliberately contains upper-case letters and spaces, like the default `~/.local/share/MechCommander2`. A screenshot is taken every 100 frames and kept with the log in `<out>/<flow>/` to show where a flow went wrong.

| Flow | Steps and checks |
|---|---|
| `options` | Main menu → Options, switch through the four tabs, Accept; the resolution must not change |
| `encyclopedia` | The encyclopedia's six categories → Close |
| `exit` | Exit → Yes; the game must quit before the timeout |
| `campaign` | New campaign → first mission (won instantly with the debug key Ctrl+Alt+Shift+W) → debriefing → mission selection → briefing → 'Mech bay → pilots → launch mission two; a campaign save must be left behind |
| `load_game` | Load Game must list the save from `campaign`; loading it enters the logistics screens |
| `solo_mission` | Solo Mission must list completed missions; selecting one enters the briefing, 'Mech bay and pilot screens |
| `quick_save` | With hotkeys: Quick Load before any save exists (the mouse must not stay locked), then Quick Save and Quick Load; the log must contain `Saved game` / `Loaded game` |
| `pause_menu` | Box-select units, then use the pause menu: Quick Save, Quick Load, Options → Accept, Exit → Yes |
| `old_campaign_save` | Load Game with the campaign save of every release in `tests/saves`; the log must show the format, 3 'mechs and 80000 C-Bills and nothing left out |
| `old_quick_save` | Quick Load of the 0.1.0 quick save; must print `Loaded game` |
| `foreign_quick_save` | Quick Load of a quick save with another build's stamp; must be refused with a message, no `Loaded game`, the mission goes on |

`load_game` and `solo_mission` use the save left by `campaign`, so run them together with it or after it.

Bugs these flows caught (all fixed):

- The options screen's text wrapping looped forever on a long graphics card name.
- Accept on the options screen changed the resolution: SDL3 reports 24-bit color, the settings file stores 32, and with no match the list picked the first entry.
- The whole user folder path was lower-cased, so every save failed on Linux.
- The mouse was not restored after a failed load.
- Pause menu buttons ignored clicks: the "was that a drag" check used the previous click's state, which was never initialized. The first click after a box selection was always swallowed.
- Accept in the pause menu's Options crashed in a mission started with `-mission`.

### Continuous integration

`.github/workflows/ci.yml` runs on every push / PR, except pushes that only change documentation (`*.md`, `doc/`; use "Run workflow" to force a run):

1. Builds RelWithDebInfo and Debug and runs every CTest test (Linux), and builds and runs the unit tests on Windows (MSVC) and macOS (Apple Silicon);
2. Downloads and builds the game data and plays `mc2_01` for 60 seconds as a smoke test;
3. Runs the screenshot and UI flow tests (both render backends) and uploads screenshots, heat maps and logs.

The macOS job also checks the contents of `mc2.app` and runs `mc2 -version`; wrong output or a crash fails the step.

Every job has `timeout-minutes`, so a hung game cannot hold CI forever.

`.github/workflows/release.yml` builds and tests the packages for all three platforms, see [Packages and releases](#packages-and-releases).

---

## Building on Windows

> CI (`windows-latest`, MSVC x64) builds everything and runs the unit tests; the release packages are tested by players.

The SDL3 libraries are downloaded and built statically by CMake (as on Linux). Install zlib, GLEW and FreeType with vcpkg
(static libraries + dynamic CRT, matching CMake's Release settings):

```bat
vcpkg install zlib glew freetype --triplet x64-windows-static-md
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo
```

The string resource `mc2res_64.dll` is built with the game; put it next to `mc2.exe` or in the game directory.
The SDL3 GPU backend uses Vulkan (SPIR-V) on Windows; D3D12 needs DXIL, which is on hold by decision.

## Building on macOS

> CI (`macos-latest`, Apple Silicon) builds everything, runs the unit tests and checks the `.app`; playing on a real Mac still needs manual testing.

The minimum is macOS 13. macOS only has the deprecated OpenGL 4.1, so the OpenGL backend is not built by default (`MC2_ENABLE_OPENGL=OFF`)
and only SDL3 GPU (Metal) is used:

```bash
brew install glslang spirv-tools spirv-cross freetype
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMC2_SHADER_REQUIRE=msl
cmake --build build -j"$(sysctl -n hw.ncpu)"
ctest --test-dir build
```

This produces `build/mc2.app` (string resource `libmc2res.dylib` in `Contents/Resources/`). `scripts/prepare_game_dir.sh`
copies `mc2.app` into the game directory and creates `./mc2` pointing at its executable for use in a terminal;
started from Finder, the game looks for `data/` next to the `.app`.

---

## Packages and releases

### The packages

| Platform | File | Contents |
|---|---|---|
| Windows 10+ (x64) | `MechCommander2-<version>-windows-x64-setup.exe` | Inno Setup installer: installs per user by default without administrator rights, or for all users into Program Files. Creates a Start menu shortcut (desktop shortcut optional), adds the LAN firewall rule (optional, on by default) and provides an uninstaller. |
| | `MechCommander2-<version>-windows-x64.zip` | Portable version: extract and run `mc2.exe` |
| macOS 13+ (Apple Silicon) | `MechCommander2-<version>-macos-arm64.dmg` | `MechCommander 2.app` with the game data in `Contents/Resources/`; drag it to Applications |
| Linux x86_64 | `MechCommander2-<version>-linux-x86_64.AppImage` | Single executable bundling GLEW and other libraries that may be missing (OpenGL, Vulkan and X11 come from the system). Needs FUSE 2 |
| | `MechCommander2-<version>-linux-x86_64.tar.gz` | The same contents; extract and run `./mc2` |

Every package includes the complete game data (about 480 MB), so no data build is needed after downloading, plus the licenses (`licenses/`) and a player `README.txt` (English and Chinese: how to start, where saves are, common options).

**Not code-signed**: Windows SmartScreen warns about an "unknown publisher", and macOS refuses the first start until you click "Open Anyway" in System Settings > Privacy & Security. Avoiding these warnings needs a code-signing certificate on Windows and an Apple developer account (USD 99 per year) plus notarization on macOS; the macOS signing in `scripts/package.py` is currently ad-hoc (the minimum Apple Silicon requires to run a program).

### Publishing a release

```bash
git tag v0.1.0
```

```bash
git push origin v0.1.0
```

`.github/workflows/release.yml` then:

1. **Linux**: builds, runs the unit tests, builds the game data from mc2srcdata (the real Git LFS videos are required; no test patterns), and produces the AppImage and tar.gz. It then runs the read-only AppImage from another folder with a fresh user folder, plays `mc2_01` for 300 frames with OpenGL and with SDL3 GPU, and checks that the screenshots are not black.
2. **Windows**: builds with MSVC and produces the installer and zip (the Visual C++ runtime DLLs sit next to `mc2.exe`). Tests a silent install, checks the files, runs `mc2.exe -version`, uninstalls.
3. **macOS**: builds and produces the dmg (re-signed ad-hoc, icon generated). Mounts it and checks the signature, the files and `mc2 -version`.
4. When all three pass, creates a **draft** GitHub Release with every package and `SHA256SUMS.txt`, using `doc/RELEASE_NOTES.md` as the text. Check it, then press "Publish" on GitHub.

The game data and the compiled shaders (SPIR-V / MSL / HLSL) are platform-independent, so they are built once on Linux and handed to Windows and macOS.
**Test versions.** Running the workflow by hand (Actions > Release > Run workflow, pick the branch) publishes a **pre-release** `v<next patch>-test.<run number>` (for example `v0.1.1-test.42` after `v0.1.0`) with the packages and `SHA256SUMS.txt`, tagged at the branch's commit. Games with "Receive test versions" on offer it as an update; everyone else never sees it (`releases/latest` skips pre-releases, and `0.1.1-test.42` sorts before `0.1.1`). Only the 5 newest test pre-releases and their tags are kept. Options: `version` sets the version by hand, unticking `publish` only keeps workflow artifacts, unticking `macos` skips the macOS job (a test pre-release does not wait for macOS anyway). Changing `packaging/` or `scripts/package.py` builds and tests the packages as `0.0.0-test` without publishing; each package is kept as a separate workflow artifact (not zipped again). Releases also carry the parts and manifests for part updates (see [Update check](#update-check)).

After publishing a new test build from `dev`, update the version and direct download link in both `README.md` and `README.zh-TW.md`.

The Windows and macOS CI machines have no usable GPU, so on those platforms only installation and startup are tested, not actual play.

Bugs the package tests caught (all fixed):

- **AppImage quit after about 8 seconds**: at startup the game checked free space in the current directory; the AppImage's read-only mount reports 0, so a "not enough disk space" dialog appeared. It now checks the user folder where saves are written.
- **Black screen and crash on Windows right after starting** (v0.1.0 draft, reported by a player): SDL3 GPU was told the game supports SPIR-V, MSL and DXIL, so on Windows SDL preferred D3D12, but the packages have no DXIL (on hold by decision); every shader failed to load, a null pointer was dereferenced, and there was no fallback to OpenGL. Now only the formats actually shipped are declared (so Windows uses Vulkan), and initialization creates a test shader first and falls back to OpenGL if that fails. CI's Windows machine has no GPU, so it cannot exercise this path.
- **`mc2 -version` did nothing on macOS; the game started and crashed**: `gameos.hpp` did `#pragma pack(push,4)` before including the standard library, which on macOS (libc++) changed the alignment of `std::string` and `std::vector` to 4, so different source files saw different struct layouts and the program read command-line options from the wrong place. `gameos.hpp` now includes the standard headers it needs before setting the packing, and `run_options.h` has `static_assert`s so a changed alignment fails to compile. In new code, include standard headers before `gameos.hpp`.

### Packaging locally

```bash
python3 scripts/package.py --platform linux --build build --game game --version 0.1.0 --out dist
```

`--game` is the game directory assembled by `scripts/prepare_game_dir.sh`; only its data is used. On Linux add `--linuxdeploy <linuxdeploy executable>` to produce an AppImage; on Windows [Inno Setup 6](https://jrsoftware.org/isinfo.php) is needed for the installer (otherwise only the zip is made); macOS uses the built-in `sips`, `iconutil`, `codesign` and `hdiutil`.

Everything else is in `packaging/`: the icon (`icon.png` / `icon.ico`, a simple original design that uses no trademarks of the original game), the Linux `.desktop` file, the Windows installer script `mc2.iss` and version resource `mc2.rc`, the player README, the third-party license list and the release notes.

---

## Known limitations and open items

### Render backends

`GameOS/gameos/render/render_device.h` defines the backend interface; game code does not know which backend is in use:

| Backend | Option | Description |
|---|---|---|
| SDL3 GPU | Default, or `-renderer gpu` | Vulkan on Linux and Windows, Metal on macOS; reads `shaders/compiled/` built at build time. Only the shader formats actually shipped are requested from SDL; there is no DXIL yet, so Windows never uses D3D12 |
| OpenGL | `-renderer gl`, or automatically when SDL3 GPU cannot start | OpenGL 4 Core Profile, compiles `shaders/` at run time; lives in a separate `gameos_gl` library that only the game links |

Without `-renderer`, SDL3 GPU is tried first; if it fails (no Vulkan driver, missing `shaders/compiled/`, or the driver cannot create the shipped shaders) the window is recreated with OpenGL. With an explicit `-renderer` there is no fallback. The OpenGL backend stays for one release cycle as a fallback and is then removed (see [Decision log](#decision-log)).

GameOS is an immediate-mode API; SDL3 GPU needs uploads outside render passes and state baked into pipelines. So `render/gpu/gpu_device.cpp` records a whole frame (pipeline, textures, parameters, changed vertex data copied into a per-frame upload ring), uploads once at the end, and replays it into an offscreen target that is then blitted to the swapchain. Static buffers are promoted to their own GPU buffers. The viewport is offset by 0.01 pixel so rasterization matches GL (menus are pixel-identical).

Shared by both backends (`gameos_graphics.cpp`):

- **Draw batching**: consecutive draws with identical state are merged; draw order is unchanged. `-no-batching` turns it off.
- **Texture atlas**: 32/64/128 pixel textures are also copied into 1024×1024 pages. They are used only when the texture coordinates stay inside the tile, so the image is identical.
- **`gos_RenderShape`**: draws a mesh with its matrices and light indices; consecutive shapes are merged. Used by the 3D model hardware path.

Result: mc2_01 went from 242 to 68 draw calls per frame, and average frame time on GL from 26.7 to 11.5 ms (Mesa).

**`-gpu-shapes`** (3D models transformed and lit on the GPU) is off by default:

- The upstream version drew every model a second time, 100 pixels to the right ("ghost" buildings). This port removed that and kept only the CPU path, as in the original game.
- The GPU path is fixed and its lighting now runs in shaders (`shaders/include/lighting.hglsl`). It matches the CPU golden images within 0.25%.
- It is slower on Mesa software rendering and still needs measuring on a real GPU.

### Benchmarks and CPU-side optimization

- `scripts/benchmark.py <game dir>` plays mc2_01 and mc2_07 for 900 frames in deterministic mode. It reports wall time and main thread CPU time.
- Use `--exe a,b` to interleave two executables for an A/B comparison. Runs at different times vary by 5–10%.
- On Mesa, wall time is mostly software rasterization. On a real GPU the main thread's CPU time (about 2.8 ms per frame) is the limit.
- Two optimization rounds found no remaining hotspot; game logic takes 5–10% per subsystem.
- `perf` does not work on some kernels. Then sample with gdb, and time with `std::chrono`: the game's own timers report simulated time in fixed-step mode.

### Shaders

`shaders/` is one set of GLSL sources used for two kinds of targets:

| Target | Compiled | Notes |
|---|---|---|
| OpenGL | At run time by the driver | GameOS prepends `#version 420` and the variant defines (e.g. `ALPHA_TEST`) |
| Vulkan / Metal / D3D12 | At build time by `scripts/build_shaders.py` | glslangValidator to SPIR-V → `spirv-val` → spirv-cross to MSL and HLSL → DXIL when `dxc` is installed |

Build output goes to `<build>/shaders_out/{spirv,msl,hlsl,dxil}/<program>.<variant>.<stage>.*`, plus `shaders.json` listing how many samplers and uniform buffers each stage uses (SDL3 GPU needs this to create shaders). `prepare_game_dir.sh` copies it to the game directory's `shaders/compiled/`. Without `glslangValidator` or `spirv-val` CMake skips this step, which does not affect the OpenGL build.

Writing rules (`shaders/include/gos_common.hglsl`):

- No `#version` and no loose `uniform`s. Every parameter C++ sets by name (`mvp`, `wvp_`, `vp`, `fog_color`, `Foreground`…) lives in the shared `PerDraw` uniform block, matching `gos_render::PerDraw` in `GameOS/gameos/render/shader_params.h`. When adding a parameter change both, and update the offset table in `build_shaders.py`.
- Uniform blocks use `GOS_UBO(GL binding point, index within the stage)`, textures `GOS_SAMPLER(unit)`. Under Vulkan these expand to `set` / `binding` per SDL3 GPU's rules (vertex stage: textures set 0, uniforms set 1; fragment stage: textures set 2, uniforms set 3).
- Every `in` / `out` between stages needs `layout(location = N)`, with matching numbers in both stages.
- `gl_Position` goes through `gos_clip()`: unchanged on OpenGL; other targets map the depth range from -1..1 to 0..1.
- With `-shader-reload`, editing files under `shaders/` recompiles them in game (development only, off by default).

### Decision log

| Question | Decision |
|---|---|
| Audio migration | Straight to SDL3_mixer (no SDL2_mixer interim step) |
| Shader source language | Keep GLSL, rewritten in Vulkan style and compiled to SPIR-V / MSL / HLSL (option A) |
| Getting SDL3 | Use an installed version if found, otherwise download and build a pinned version (`MC2_SDL3_SOURCE=AUTO\|SYSTEM\|FETCH`) |
| Minimum platforms | Ubuntu 24.04, Windows 10, macOS 13 |
| OpenGL fallback | Once SDL3 GPU is the default, keep the OpenGL backend for one release cycle, then remove it |
| Viewer (model viewer) | Not needed, removed |
| Phase 7 stop condition | Under 100 draw calls per frame in mc2_01 |
| DXIL output | On hold: needs Microsoft's prebuilt dxc downloaded in CI, to be decided separately |
| License headers on new files | Source, scripts, shaders, CMake and CI files added by this port carry `SPDX-License-Identifier: GPL-3.0-only`; Microsoft's original files are left as they are |
| Network play | In scope: LAN play is done; the other modes are in the roadmap |

### Audio output

- The mixer runs at the device's rate (usually 48 kHz, float). Without a device, as in the tests, it runs at 22.05 kHz.
- Each track is mixed at half volume and `gosLimiter` restores the level, so the sum never hard-clips.
- The device buffer defaults to 2048 frames. Override it with `SDL_AUDIO_DEVICE_SAMPLE_FRAMES`.
- The effect on Windows still needs confirming on real hardware.

### Memory and undefined behaviour checks

```bash
cmake -S . -B build-asan -DMC2_ENABLE_ASAN=ON -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-sanitize=alignment,vptr" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined"
```

```bash
cmake --build build-asan -j
```

- Run with `ASAN_OPTIONS=detect_leaks=0`. The original code has static memory pools that are never freed, so `unit_stuff` leak reports are noise.
- Add `-DCMAKE_BUILD_TYPE=Debug` to get asserts too. Debug builds stop at PAUSE unless `MC2_NO_DIALOGS` is set.
- Missions, all flow tests and the LAN scenarios have been run this way and come out clean. The fixes are listed by `git log -i --grep=asan`.
- ASan's allocator needs the sized delete and nothrow new overloads that the game provides.

### Other open items

- Whether `-gpu-shapes` becomes the default: it needs measuring on a real GPU.
- The OpenGL fallback is removed after one release cycle (see the decision log). DXIL is on hold.
- Not yet verified on real Windows and macOS machines, real GPUs, or in long sessions. Game CI runs only on Linux with Mesa.
- Compiler warnings are at 0 (GCC 13). Keep them there.
- The `HACK` and `FIXME` comments in `code/`, `mclib/` and `GameOS/include/platform_*.h` come from the original or upstream sources and describe deliberate behaviour. Leave them alone.
