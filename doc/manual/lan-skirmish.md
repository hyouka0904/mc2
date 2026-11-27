# LAN Skirmish Manual

[中文版](lan-skirmish.zh-TW.md) ｜ [Back to README](../../README.md)

2–8 computers on the same local network (or a virtual LAN) play against each other on the original multiplayer maps, using the original lobby screens. Windows, macOS and Linux can play together.

## Before you start

- Everyone needs the **same version** of the game. In the room list, a room you cannot join shows the host's version (`VERSION x.y.z`) in the map column, or `DIFFERENT GAME DATA` when the versions match but the data or mods differ. Pressing **Join** shows both versions; if the host's version is newer you can open the download page from there. The main menu also tells you when a new version is out.
- Only the **hosting computer** must accept incoming connections; players who join need no setup. See [Firewall](#firewall) below.

## How to play

1. Main menu → **Multiplayer** → **Local Network**.
2. The host presses **Host Game**, enters a room name → **Accept**.
3. Everyone else presses **Go to LAN Browser**, picks the room → **Join**. The room list broadcasts a query on the LAN every second, so rooms on the same network appear by themselves; no IP needed.
   - If no room shows up (different subnet, broadcasts blocked), choose **Internet (Direct TCP/IP)** instead, enter the host's IP → **Join Game**.
4. The host picks the map and rules. Everyone picks a team and faction, then selects **Prepare**. Read the briefing, assemble a lance and assign pilots, then select **Finish prep** to return to the room.
5. The host can select **Start game** once everyone is ready, within budget and meets the room rules. Rule changes require players to prepare again.

**Main Menu** (top right) leaves the room (disconnects) and returns to the title menu without asking.

The chat recipient menu offers everyone, your team or one player. Select the recipient, type your message, and press **Enter**.

## During the mission

- You can only select and command your own units; click a teammate's unit to see its info.
- **P** pauses the whole game: 3 pauses per player per match, and anyone can unpause.
- **Esc** only opens your own menu; the battle keeps going.
- **Enter** chats with everyone, **Shift+Enter** with your team.
- Everything else works as in single player; press **F1** for the full hotkey list.
- If a player drops, the host's AI takes over their units; if the host drops, the mission ends for everyone.

## Finding your IP address

- The **Local Network** panel (below the help box) and the LAN room list (right of the Host Game button) show `YOUR IP ADDRESS`.
- Once you host, the bottom left of the room screen shows `HOST IP ADDRESS`; this is what others type for a direct IP join.
- With several network adapters (VPN, VirtualBox, Hyper-V, Tailscale) the line shows the address of the default route followed by the number of others, e.g. `192.168.1.23 (+2)`. Hover the line to list every address with its adapter name; on a typical home network the one to use starts with `192.168.` or `10.`.
- On Windows you can also run `ipconfig` in a command prompt and look for "IPv4 Address".

## Firewall

The game uses a single port: **UDP 27015** (shared by the game and room discovery).

| System | What to do |
|---|---|
| **Windows** | The installer's "Allow LAN multiplayer through Windows Firewall" option is checked by default; it asks for administrator approval once and adds a firewall rule for `mc2.exe` (removed on uninstall). With the zip version, or with the option unchecked, Windows asks the first time you host: choose "Allow". |
| **Linux** | Ubuntu has no firewall enabled by default, so nothing to do. With ufw enabled run `sudo ufw allow 27015/udp`; with firewalld (on by default on Fedora) run `sudo firewall-cmd --add-port=27015/udp`. |
| **macOS** | The system firewall is off by default; if it is on, macOS asks the first time you host whether to allow incoming connections: choose "Allow". |

## Room not found or cannot connect

1. Make sure both computers are on the same router or Wi-Fi (guest networks usually block traffic between devices).
2. Use **Internet (Direct TCP/IP)** with the `HOST IP ADDRESS` shown on the host's screen.
3. Check the firewall on the hosting computer (see the table above).
4. If it still fails, start the game with network logging and send the resulting `netlog.txt` and a screenshot to the developers (with the Linux AppImage, replace `./mc2` with the AppImage's file name):
   ```bash
   MC2_NET_LOG=2 ./mc2 > netlog.txt 2>&1
   ```
   On Windows, open a command prompt in the game folder and run, one after the other:
   ```bat
   set MC2_NET_LOG=2
   ```
   ```bat
   mc2.exe > netlog.txt 2>&1
   ```

## Playing over the internet

When you are not on the same LAN, everyone installs a virtual LAN such as [Tailscale](https://tailscale.com/) or [ZeroTier](https://www.zerotier.com/), then joins with **Internet (Direct TCP/IP)** using the host's virtual LAN IP.

## Current limitations

- Salvage Craft and withdrawing units from the battlefield are not synchronized yet; turn off Salvage Craft in the room settings.
