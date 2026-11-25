# MechCommander 2 Open-Source Port

[中文版](README.zh-TW.md)

## [⬇ Download the latest version](https://github.com/hyouka0904/mc2/releases/latest)

Pick the file for your platform under Assets at the bottom of the release page:

| Platform | File |
|---|---|
| Windows 10+ (x64) | `…-windows-x64-setup.exe` (installer) or `…-windows-x64.zip` (portable) |
| macOS 13+ (Apple Silicon) | `…-macos-arm64.dmg` |
| Linux x86_64 | `…-linux-x86_64.AppImage` or `…-linux-x86_64.tar.gz` |

Every package contains the complete game data (about 600 MB) and is ready to play. See `README.txt` in the package for installation steps, where saves live and common options. The game checks for a new version at start (at most once a day) and shows a notice in the main menu; click it to download and install the update (the game restarts; saves are kept). The Windows versions and the Linux tarball download only the parts that changed, usually a few MB. Options > Gameplay > "Receive test versions" also offers unreleased test builds. `-noupdatecheck` turns the check off.

> **This project is an independent fork of [alariq/mc2](https://github.com/alariq/mc2). It is not affiliated with or endorsed by alariq, nor by Microsoft.**
> Please report issues here, not to the upstream project.

---

## About

MechCommander 2 is a real-time tactics game released by Microsoft in 2001. You command a lance of BattleMechs: between missions you buy and refit 'Mechs, choose weapons and assign pilots; during missions you give move and attack orders.

Microsoft released the game source under the Shared Source license, but its engine, GameOS, shipped as headers only. alariq reimplemented GameOS on SDL2 + OpenGL. This fork moved it to SDL3 with Vulkan / Metal rendering by default, so the game runs as a 64-bit program on Windows, macOS and Linux, and fixes many bugs that were already in the original game.

- The single-player campaign, single missions, saving and loading, and the 'Mech bay all work.
- **LAN multiplayer**: 2–8 computers on one local network; Windows, macOS and Linux can play together.
- Mods and modpacks.
- More multiplayer modes (co-op missions, roguelike, 1v1) are in development.

**[Development guide (building, architecture, tests, releases) →](DEVELOPMENT.md)**

---

## Multiplayer manuals

Single player plays as in the original game; press **F1** in a mission for the full hotkey list.

| Mode | Manual | Status |
|---|---|---|
| LAN skirmish (the original multiplayer mode) | **[LAN skirmish manual](docs/manual/lan-skirmish.md)** | Playable |
| Co-op missions (assault, escort, salvage, recon) | When it ships | In development |
| Roguelike assault / defense | When it ships | Planned |
| 1v1 (capture, annihilation) | When it ships | Planned |

---

## Other

- [Mods / modding / modpacks](docs/modding.md): change game data with mods; the `mc2mod` tool.
- [Multiplayer and Chinese translation roadmap](ROADMAP_MULTIPLAYER_ZH.md) (Chinese only).
- [Upstream author's website](https://alariq.github.io/mc2-website/).

### Differences from upstream

This fork branched from upstream `7130807` (master, April 2022). Below is what this fork added and which fixes came from the upstream author.

#### Added in this fork
- **Rendering**: SDL2 replaced by SDL3 (with SDL3_mixer and SDL3_ttf); a new SDL3 GPU backend is the default (Vulkan on Linux / Windows, Metal on macOS) with OpenGL as a fallback; shaders are compiled to SPIR-V and MSL at build time; draw batching and a texture atlas bring the first mission under 100 draw calls; TG model lighting moved into shaders.
- **Platforms and releases**: macOS support (`.app`); packages and a release workflow for all three platforms, with CI installing and starting each package; saves and settings moved to the system user-data folder with automatic migration; version number and `-version`; free disk space check at startup; slimmer Win32 platform layer.
- **Multiplayer**: LAN play (ENet, host-authoritative simulation, room discovery, disconnect handling, network pause).
- **Testing**: unit tests, headless mission smoke tests, fixed-step screenshot comparison tests, UI flow tests, network tests, benchmark scripts, and CI on Linux, Windows and macOS.
- **Features**: mod / modpack system and the `mc2mod` tool; 3D audio API; memory allocation tracking.
- **Fixes and cleanup**: builds with GCC 13; compiler warnings 643 → 0; A* pathfinding bug and open-list performance; RLE TGA decoding; quick save / load, the options screen and the pause menu; struct layout broken by `#pragma pack(4)` on macOS; memory errors and undefined behaviour found with AddressSanitizer + UBSan; audio mixed at the device sample rate with a limiter. Details in the [development guide](DEVELOPMENT.md).

#### Fixes from the upstream author
These fixes come from upstream work after this fork branched and were ported here (upstream commits in brackets):

- `RollDice()` uses 15-bit random numbers so dice rolls are correct on Linux (`1c601fc`)
- An options file missing `ResolutionY` or `AltitudeMaximumLo` no longer resets the other value (`88412ec`, `85adda9`)
- Components with ids above 127 no longer show the wrong abbreviation in the info window (`3bfc4ae`)
- Pressing a key that is already bound while setting a hotkey no longer loops forever (`8dd96f8`)
- `\` in data file paths becomes `/` on non-Windows systems (`a22583e`, `73edc36`)
- Alpha test discards only fully transparent pixels, as the original renderer did, which brings back the compass and minimap frame (`29b5a88`)

These features follow the upstream approach but were reimplemented on this fork's architecture: movie playback (pl_mpeg), the `-datapath` option, and relayout of the mission interface when only the window height changes.

#### In upstream but not in this fork
- The hardware T&L render path: this fork's SDL3 GPU backend works differently, so it does not apply.
- Prebuilt Windows third-party libraries and their build guide: this fork uses vcpkg, see [Building on Windows](DEVELOPMENT.md#building-on-windows).
- Multi-monitor window handling (displayIndex).

### License

- The original game code was released under the Shared Source Limited Permission License, see `EULA.txt`.
- alariq's GameOS reimplementation is licensed under GPL v3, see `license.txt`; this fork's changes are GPL v3 as well.
- Files added by this port start with `SPDX-License-Identifier: GPL-3.0-only`; files without that line fall under the two points above.
- Whether GPL v3 can coexist with clause (E) of the Shared Source license (source may only be redistributed under that license) is unclear; get legal advice before any public or commercial distribution. The game art may only be used non-commercially.

> From the upstream author: because of the war in Ukraine, the author currently has no time to maintain the project; community help is welcome.
