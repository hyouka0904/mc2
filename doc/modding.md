# Mods / Modding / Modpacks

[中文版](modding.zh-TW.md) ｜ [Back to README](../README.md)

| Term | Meaning in this project |
|---|---|
| **Mod** | A folder: `mod.ini` (description) + `content/` (files). `content/` mirrors the game directory; its files **replace** game files (including files packed in `.fst` archives) or **add** new ones. |
| **Modding** | Creating, checking and debugging mods with the `mc2mod` tool, or using the `mods/mod_system.h` API from code. |
| **Modpack** | A `modpack.ini` listing which mods to enable and which versions they need. It can override priorities and can ship its own mods in a `mods/` folder next to it. |

## What you can change

Almost all of MechCommander 2's content is data files, so mods can change or add:

| Content | Path (under `content/`) |
|---|---|
| Stats for 'Mechs, vehicles, weapons, pilots (items) | `data/objects/*.csv`, `*.fit` |
| Missions and maps (a new mission can be started with `-mission <name>`) | `data/missions/`, `data/terrain/` |
| Game logic and AI (ABL scripts) | `data/missions/*.abl`, `data/missions/warriors/` and others |
| UI layouts and images | `data/art/*.fit`, `*.tga` |
| Sounds, fonts, shaders | `data/sound/`, `assets/graphics/`, `shaders/` |

File names are case-insensitive (the game lower-cases names before opening), and `\` and `/` are treated the same.

## Mod layout

```
mods/
└── laser_rebalance/
    ├── mod.ini
    └── content/
        └── data/objects/...
```

```ini
[mod]
id = laser_rebalance          ; lower-case letters, digits, _ . -; must be unique
name = Laser Rebalance
version = 1.2.0
author = Your Name
description = Rebalances laser weapons
priority = 0                  ; higher loads later; for the same file the later mod wins
enabled = true                ; enabled when no modpack is used

[dependencies]                ; mods that must be present, with version requirements
core_tweaks = ^1.0

[optional]                    ; only affect load order when present
fancy_ui = >=2.0

[conflicts]                   ; mods (and versions) that cannot be used together
old_lasers = *
```

Version requirements: `*`, `1.2.3` (exactly), `>=1.0`, `>1.0`, `<=2`, `<2`, `^1.2` (≥1.2.0 and <2.0.0),
`~1.2` (≥1.2.0 and <1.3.0); join several with commas for "and": `>=1.0, <1.5`.

## Load order

1. A mod always loads **after** the mods it depends on (an installed dependency that was not selected is added automatically).
2. Otherwise mods load by `priority`, lowest first, then by id alphabetically.
3. When several mods provide the same file, **the last one loaded wins**.
4. On a missing dependency, version mismatch, conflict or dependency cycle, the game lists the reasons and exits with code 3 rather than running with a broken setup.

## Modpacks

```ini
[modpack]
id = hardcore
name = Hardcore Pack
version = 1.0.0

[mods]                        ; mods to enable = version requirement (only these are enabled)
laser_rebalance = ^1.2
core_tweaks = *

[priority]                    ; optional: override individual mod priorities
laser_rebalance = 50
```

If the modpack folder has a `mods/` subfolder, the mods in it take precedence over mods with the same id in the global mod folder.

## Using mods in the game

| How | Effect |
|---|---|
| A `mods/` folder in the game directory | Every mod in it with `enabled = true` is used |
| `./mc2 -mods <folder>` | Use the enabled mods in that folder |
| `./mc2 -mods <folder> -modpack <modpack.ini>` | Use only the mods listed in the modpack |

With mods active, the game prints the load order and the list of overridden files on stderr, in lines starting with `MODS:`.

## The mc2mod tool

After building, it is at `build/out/tools/mc2mod/mc2mod`:

```bash
mc2mod new mods laser_rebalance "Laser Rebalance"
```

```bash
mc2mod validate mods/laser_rebalance
```

```bash
mc2mod list mods
```

```bash
mc2mod resolve mods --pack packs/hardcore/modpack.ini
```

```bash
mc2mod which mods data/objects/pilots.csv
```

```bash
mc2mod new-pack packs/hardcore/modpack.ini hardcore laser_rebalance=^1.2 core_tweaks
```

| Command | Description |
|---|---|
| `new` | Create a mod skeleton (`mod.ini`, `content/data/...`, README) |
| `validate` | Check the manifest, that `content/` is not empty, and for file names that differ only in case |
| `list` | List installed mods |
| `resolve` | Show the load order, files provided by several mods, and all errors (exit code 1 on errors) |
| `which` | Show which mod finally provides a game file and what it overrides |
| `new-pack` | Write a modpack manifest |

## Programming interface

- `GameOS/gameos/mods/mod_system.h` (namespace `mc2mod`): `Version` / `VersionReq`, `ModManifest`, `ModpackManifest`,
  `ModRegistry`, `resolveLoadOrder()`, `Overlay` (virtual file system), `describe()`, `createModSkeleton()`, `validateMod()`.
  It does not depend on the rest of the game, so tools and other programs can use it.
- `GameOS/gameos/mods/gos_mods.h`: the game-side interface `gos_ModsInit()`, `gos_ModResolvePath()`, `gos_ModsShutdown()`.
  `File::open()` and `fileExists()` in `mclib/file.cpp` ask the overlay before reading, so any data read through the game's file layer can be replaced by a mod.

## Examples

`examples/mods/` has two example mods; both replace the main menu background `data/art/mcl_mm_1.tga` (originally inside `art.fst`):
`red_main_menu` (red) and `blue_main_menu` (blue, `priority = 5`). With both enabled, blue wins;
`examples/modpacks/red_menu_pack` overrides that with `[priority]` so red wins.

```bash
./mc2 -mods ../examples/mods
```

```bash
./mc2 -mods ../examples/mods -modpack ../examples/modpacks/red_menu_pack/modpack.ini
```

## Tests

| Test | What it covers |
|---|---|
| `unit_mods` | 18 cases: versions and requirements, INI parsing, manifest error reporting, modpacks, dependency ordering, automatically added dependencies, missing dependency / version mismatch / cycle / conflict, optional dependencies, overlay and case handling, skeleton and validation, game-side hooks |
| `tool_mc2mod` | Runs the whole workflow through `mc2mod` (create → add content → validate → add dependency → resolve → which → broken modpack) and checks the examples |
| Screenshot tests `main_menu_mods` / `main_menu_modpack` | Start the game with the example mod and modpack and check that the menu background turns blue and red respectively |
