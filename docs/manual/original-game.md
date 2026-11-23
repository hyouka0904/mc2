# Original Game Manual

[中文版](original-game.zh-TW.md) ｜ [Back to README](../../README.md)

This manual covers single player: installing, the menus, the campaign, the 'Mech bay and mission controls. For multiplayer see the [LAN skirmish manual](lan-skirmish.md).

## Installing and starting

Download the file for your platform from the [README](../../README.md).

| Platform | Steps |
|---|---|
| **Windows** | Run `…-setup.exe`, follow the installer, then start MechCommander 2 from the Start menu. The program is not code-signed, so SmartScreen may warn: "More info" → "Run anyway". Portable version: extract the zip and run `mc2.exe`. |
| **macOS** | Open the `.dmg`, drag "MechCommander 2" to Applications and open it. The app is not notarized, so the first start is blocked: go to System Settings → Privacy & Security and click "Open Anyway". |
| **Linux** | AppImage: run `chmod +x MechCommander2-*.AppImage`, then start it (needs FUSE 2: package `libfuse2` or `libfuse2t64`). Tarball: extract it and run `./mc2` in the folder. |

To update, install the new version over the old one; saves and settings are kept.

## Main menu

| Button | What it does |
|---|---|
| **Campaign** | Start a new campaign or continue the current one |
| **Solo Mission** | Replay unlocked missions on their own, without affecting the campaign |
| **Multiplayer** | Play with others, see the [LAN skirmish manual](lan-skirmish.md) |
| **Load Game** | Load a saved game |
| **Options** | Graphics, audio, control style and hotkeys |
| **Encyclopedia** | Data on 'Mechs, vehicles, weapons and pilots |
| **Exit** | Quit |

## The campaign

Every mission goes through these screens before you deploy:

1. **Mission briefing**: objectives, map and expected enemy forces.
2. **Mech Bay**: choose the 'Mechs to deploy. Buy and sell 'Mechs with Resource Points (RP); **MechLab** lets you change a 'Mech's weapons and armor. Watch the total weight, heat and where each weapon can be mounted.
3. **Pilots**: assign a pilot to each 'Mech. Pilots gain experience in missions and their skills improve; pilots who die are gone for good.
4. **Launch**: start the mission.

After a mission the debriefing shows completed objectives, RP earned, and enemy 'Mechs and components you can salvage.

## Controls

### Mouse

Default control style (Options can switch back to the original single-button style):

| Action | Result |
|---|---|
| Left-click a unit | Select it |
| Left-drag | Box-select several units |
| Right-click the ground | Move there |
| Right-click an enemy | Attack it |
| Hold **Ctrl** and right-click | Add waypoints, followed in order |

### Hotkeys

Press **F1** in a mission for the full hotkey list; hotkeys can be changed in Options. These are the defaults.

**Unit orders**

| Key | Order |
|---|---|
| **S** / **M** / **L** | Attack at short / medium / long range |
| **D** | Attack at optimal range |
| **C** | Fire from current position (do not move) |
| **F** | Force fire (also at buildings or the ground) |
| **G** | Guard |
| **H** | Hold position: do not move until given a move or capture order |
| **J** | Jump (for 'Mechs with jump jets) |
| Hold **Space** | Run |
| **W** | Conserve ammo |
| **A** | Energy weapons only (conserve ammo) |
| Numpad **2** / **5** / **8** | Aim at legs / arms / head |
| **Backspace** | Stop, cancel the current order |
| **Page Down** / **Page Up** | Power down (to cool off) / power up |
| **End** | Eject the pilot |
| **V** | Special unit order (e.g. repair or salvage vehicles) |
| Numpad **\*** / **/** | Airstrike / sensor probe (when support is available) |
| **I** | Info on the selected unit |

**Selection and groups**

| Key | Result |
|---|---|
| **Ctrl + 0–9** | Make the selected units group 0–9 |
| **0–9** | Select that group |
| **Shift + 0–9** | Add that group to the selection |
| **E** | Select all your units on screen |
| **Shift + E** | Add the units on screen to the selection |

**Camera and interface**

| Key | Result |
|---|---|
| Arrow keys | Scroll |
| **Shift + arrow keys** | Rotate and tilt |
| **+** / **-** | Zoom in / out |
| **Home** | Center on the selected unit |
| **F2**–**F5** | Camera presets; **Ctrl + F2–F5** stores the current view there |
| **N** | Jump to the next nav marker |
| **F9** | Show objectives |
| **Tab** / **Shift + Tab** | Switch interface tabs |
| **\\** | Show or hide the compass |
| **Esc** | Pause and open the menu (save, load, options, exit) |

## Saves and settings

Saves and settings live in the folders below. Uninstalling the game does not delete them.

| System | Location |
|---|---|
| Windows | `%APPDATA%\MechCommander2\` |
| macOS | `~/Library/Application Support/MechCommander2/` |
| Linux | `~/.local/share/MechCommander2/` |

In a mission, **Esc** gives you Quick Save and Quick Load.

## Command-line options

| Option | Effect |
|---|---|
| `-version` | Print the version and exit |
| `-windowed` | Always run in a window |
| `-renderer gl` | Render with OpenGL (try it with old graphics cards or broken graphics) |
| `-mods <folder>` | Load mods, see the [modding guide](../modding.md) |
| `-mission <name>` | Start a mission directly, e.g. `-mission mc2_01` |

On Windows, add options after the path in a shortcut's "Target" field; on macOS and Linux run the game from a terminal.

## Troubleshooting

| Problem | What to try |
|---|---|
| Black screen or crash at start | Start with `-renderer gl`; update your graphics driver |
| Fullscreen looks wrong | Start with `-windowed`, or change the resolution in Options |
| The system blocks the first start | See the SmartScreen / Gatekeeper notes under [Installing and starting](#installing-and-starting) |
| Anything else | Open an [issue](https://github.com/hyouka0904/mc2/issues) with your system, version (`-version`) and a screenshot of the error |
