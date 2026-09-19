## Notes

Thanks to [Magnus Auvinen and the Teeworlds contributors](https://github.com/teeworlds/teeworlds) for creating this game, a tight little 2D shooter where the grappling hook matters more than the gun. It connects straight to the public server list, so there are real people to play against the moment it launches.

Launching "Teeworlds.sh" opens a menu (D-Pad to move, A to confirm, B to cancel) to pick between the online client, a local dedicated server, or a local server with AI bots, so all three modes live behind one script instead of three.

## Controls

| Key | Action |
|--|--|
| Left Stick | Move |
| D-Pad | Spectator prev/next (left/right); on-screen keyboard navigation when open |
| A | Jump |
| X | Fire |
| B | Hook |
| Y | Scoreboard |
| L1 | Previous weapon |
| R1 | Next weapon |
| R2 | Fire (trigger) |
| Right Stick | Aim |
| Start | Chat |
| Select + D-Pad Down | Toggle on-screen keyboard |
| Guide | Menu |

Movement is on the left stick only, not the D-Pad. The D-Pad sends the actual arrow keys instead (Teeworlds doesn't bind arrows to anything while alive, only spectator prev/next), which is what makes the on-screen keyboard below able to reuse the D-Pad for its own navigation with zero risk of the two getting out of sync.

Hold L2 for a second layer:

| Key + L2 | Action |
|--|--|
| A | Team chat |
| B | Whisper |
| X | Show chat log |
| Y | Statboard |
| L1 | Vote yes |
| R1 | Vote no |
| R2 | Screenshot |
| Start | Emoticon |
| Select | Toggle spectator mode |

## On-screen keyboard

Hold Select and tap D-Pad Down to open a full QWERTY on-screen keyboard ([OmniOSK](https://github.com/binarycounter/OmniOSK)) for typing chat messages, server addresses, or player names without a physical keyboard. The same Select+Down chord closes it again. While it's open: D-Pad moves focus around the grid, A confirms/selects the highlighted key, hold Select+B backspaces, hold Select+X switches character pages (letters/numbers/symbols). Selecting the on-screen Submit key sends the text to the game and closes the keyboard on its own.

This is deliberately built so gptokeyb2 never has to track whether the keyboard is open or closed: the D-Pad and confirm key work identically either way, so there's no separate "keyboard mode" to fall out of sync with OmniOSK closing itself on submit. Only backspace and charset-switch need Select held, since those two would otherwise collide with in-game actions.

Since Select is now the keyboard-toggle modifier, the in-game menu moved to the Guide/Home button (client only; not present on every device).

## Local / LAN play

"Vanilla (Server)" in the menu starts a dedicated server on the device (8 players, deathmatch on dm1, LAN-only by default, no master server registration). Anyone on the same network can connect to it from their own Teeworlds client at the device's IP, port 8303. Edit `teeworlds/server.cfg` to change the map, gametype, player count, or to set `sv_register 1` if internet-facing hosting is wanted instead.

Both server options show a splash screen (title art plus "Server Running" / "Bot Server Running") for as long as the server is up, since a dedicated server has no game window of its own otherwise. Start+Select quits it as usual.

## Singleplayer / bots

Vanilla Teeworlds has no offline mode or AI, it's pure PvP. "Bot Server" in the menu instead runs [nheir's bMod](https://github.com/nheir/teeworlds/tree/server_bot0.7), a community server mod that fills empty slots with simple AI bots (pathfinding, weapon prediction, no teamwork) so there's someone to shoot at without anyone else online. Connect to it the same way as the regular local server, from the client's Local tab or `localhost:8303`. Edit `teeworlds/server_bot.cfg` to change the bot count (`sv_bot_slots`) or map.

## Compile

```
cd teeworlds-0.7.5-src
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCLIENT=ON -DDOWNLOAD_GTEST=OFF -DPREFER_BUNDLED_LIBS=OFF ..
cmake --build . --target teeworlds -j$(nproc)
```

Needs `cmake`, `pkg-config`, `libsdl2-dev`, and `libfreetype6-dev` (or `libfreetype-dev`) installed. Wavpack, pnglite, and the OpenSSL crypto hash all fall back to Teeworlds' own bundled implementations automatically if the system libraries aren't present, so nothing else needs to be installed for those. The `data/` folder needed at runtime is generated automatically by CMake at configure time as a straight copy of `datasrc/`, no separate content-build step required.

The bot server is a separate source tree ([nheir/teeworlds, `server_bot0.7` branch](https://github.com/nheir/teeworlds/tree/server_bot0.7)), built the same way but with `-DCLIENT=OFF` and targeting `teeworlds_srv` only; it shares the same `data/` folder as the regular client and server.

The launcher menu (`menu_picker`) is a small standalone SDL2 tool (source in this port, not upstream Teeworlds) that draws the loader background and a gamepad-navigable gold selector frame, then prints the chosen option to a result file for the launch script to branch on. Build with `gcc $(sdl2-config --cflags) menu_picker.c -o menu_picker $(sdl2-config --libs) -lm`; PNG decoding is via the bundled `stb_image.h` (public domain), so no `SDL2_image` dependency is needed.

The on-screen keyboard is [OmniOSK](https://github.com/binarycounter/OmniOSK), built separately and shipped as `libs.$DEVICE_ARCH/libomni_osk.so`:

```
git clone --recurse-submodules https://github.com/binarycounter/OmniOSK.git
cmake -S OmniOSK -B build -DCMAKE_BUILD_TYPE=Release -DOMNI_BUILD_TESTS=OFF -DOMNI_BUILD_EVDEV=OFF -DOMNI_BUILD_RENDERER_TEST=OFF
cmake --build build
```

Needs SDL2 >= 2.0.18 at build time for the Dear ImGui SDL Renderer2 backend it uses; if the local SDL2 dev package is older, build a newer SDL2 from source and point `PKG_CONFIG_PATH` at its `.pc` file before configuring (the resulting `.so` still runs fine against an older SDL2 at runtime, since SDL2 keeps strict backward ABI compatibility).
