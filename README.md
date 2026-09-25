## Notes

Thanks to [Magnus Auvinen and the Teeworlds contributors](https://github.com/teeworlds/teeworlds) for creating this game, a tight little 2D shooter where the grappling hook matters more than the gun. It connects straight to the public server list, so there are real people to play against the moment it launches.

## Requirements

Ready to run as-is: the client, both server modes, every map, and the on-screen keyboard are all bundled in this zip, no separate download needed. Just make sure PortMaster itself is up to date; it self-updates whenever it's run, so this is normally already the case.

Wi-Fi is required to actually play. There's no true offline singleplayer: "Vanilla (Client)" needs internet to reach the public server list, and "Bot Server" still needs a second device on the same network running its own Teeworlds client to connect in from, since the server itself is headless with no game window of its own.

## Controls

| Key | Action |
|--|--|
| Left Stick | Move |
| Right Stick | Aim |
| D-Pad Left/Right | Move (same as left stick) |
| D-Pad Up | Jump |
| D-Pad Down | Menu (Escape) |
| A | Fire |
| B | Next weapon |
| X | Jump |
| Y | Previous weapon |
| L1 | Hook |
| R1 | Fire |
| Start | Chat |
| Select + D-Pad Down | Toggle on-screen keyboard |
| Guide | Menu |

Movement lives on both the left stick and D-Pad left/right. Jump is doubled up too (X and D-Pad Up), since it's the single most-pressed button in the game. Fire is on both A and R1 (a face button plus a trigger, so either grip style works), and hook gets its own dedicated shoulder button (L1).

Hold L2 for a second layer:

| Key + L2 | Action |
|--|--|
| A | Team chat |
| B | Whisper |
| X | Show chat log |
| Y | Statboard |
| D-Pad Up | Scoreboard |
| L1 | Vote yes |
| R1 | Vote no |
| R2 | Screenshot |
| Start | Emoticon |

Hold R2 for spectator controls:

| Key + R2 | Action |
|--|--|
| D-Pad Left | Spectate previous |
| D-Pad Right | Spectate next |
| Back | Toggle spectator mode |

## Menu

Launching "Teeworlds.sh" opens a picker (D-Pad to move, A or B to confirm) with three options:

- **Vanilla (Client)**: the regular online client. Connects straight to the public server list, so there are real people to play against the moment it launches.
- **Vanilla (Server)**: starts a local dedicated server on the device (8 players, deathmatch on dm1, LAN-only by default, no master server registration). Anyone on the same network can connect to it from their own Teeworlds client at the device's IP, port 8303. Edit `teeworlds/server.cfg` to change the map, gametype, or player count, or set `sv_register 1` for internet-facing hosting instead.
- **Bot Server**: vanilla Teeworlds has no offline mode, it's pure PvP. This option runs a community server mod ([nheir's bMod](https://github.com/nheir/teeworlds/tree/server_bot0.7)) that fills empty slots with simple AI bots (pathfinding, weapon prediction, no teamwork), so there's someone to shoot at without anyone else online. Edit `teeworlds/server_bot.cfg` to change the bot count (`sv_bot_slots`) or map.

Both server options connect the same way as the regular local server, from the client's Local tab or `localhost:8303`, and show a splash screen for as long as the server is running, since a dedicated server has no game window of its own. Start+Select quits it.

## On-screen keyboard

Hold Select and tap D-Pad Down to open a full QWERTY on-screen keyboard ([OmniOSK](https://github.com/binarycounter/OmniOSK)) for typing chat messages, server addresses, or player names without a physical keyboard. The same Select+Down chord closes it again. While it's open: D-Pad moves focus around the grid, Start confirms/selects the highlighted key, hold Select+B backspaces, hold Select+X switches character pages (letters/numbers/symbols). Selecting the on-screen Submit key sends the text to the game and closes the keyboard on its own.

Since Select is the keyboard-toggle modifier, the in-game menu lives on the Guide/Home button (client only; not present on every device) -- D-Pad Down works as a standalone Escape too, for devices without one.
