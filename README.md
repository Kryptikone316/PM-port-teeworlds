## Notes

Thanks to [Magnus Auvinen and the Teeworlds contributors](https://github.com/teeworlds/teeworlds) for creating this game, a tight little 2D shooter where the grappling hook matters more than the gun. It connects straight to the public server list, so there are real people to play against the moment it launches.

Launching "Teeworlds.sh" opens a menu (D-Pad to move, A or B to confirm, Back to cancel) to pick between the online client, a local dedicated server, or a local server with AI bots, so all three modes live behind one script instead of three. A and B both confirm since SDL's A/B mapping for a given controllerdb entry isn't consistent across devices -- no need to guess which one is "right" on a given pad.

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

Movement lives on both the left stick and D-Pad left/right, matching how most games use the D-Pad. Jump is doubled up too (X and D-Pad Up) since it's the single most-pressed button in the game. Fire is on both A and R1 (a face button plus a trigger, so either grip style works) and hook gets its own dedicated shoulder button (L1) instead of sharing a face button with anything else.

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

Hold R2 for spectator controls, grouped under one modifier since D-Pad left/right now drive movement instead of spectate prev/next:

| Key + R2 | Action |
|--|--|
| D-Pad Left | Spectate previous |
| D-Pad Right | Spectate next |
| Back | Toggle spectator mode |

## On-screen keyboard

Hold Select and tap D-Pad Down to open a full QWERTY on-screen keyboard ([OmniOSK](https://github.com/binarycounter/OmniOSK)) for typing chat messages, server addresses, or player names without a physical keyboard. The same Select+Down chord closes it again. While it's open: D-Pad moves focus around the grid, Start confirms/selects the highlighted key, hold Select+B backspaces, hold Select+X switches character pages (letters/numbers/symbols). Selecting the on-screen Submit key sends the text to the game and closes the keyboard on its own.

This is deliberately built so gptokeyb2 never has to track whether the keyboard is open or closed: the D-Pad and confirm key work identically either way, so there's no separate "keyboard mode" to fall out of sync with OmniOSK closing itself on submit. Only backspace and charset-switch need Select held, since those two would otherwise collide with in-game actions. Since D-Pad now drives real gameplay (movement/jump/menu) instead of doing nothing while alive, OmniOSK's own navigation keys are explicitly remapped (`OMNI_UP_KEY`/`OMNI_DOWN_KEY`/`OMNI_LEFT_KEY`/`OMNI_RIGHT_KEY` in Teeworlds.sh) to whatever the D-Pad actually sends, rather than relying on its arrow-key defaults. Confirm reuses Start's existing "t" (chat) key rather than a dedicated button -- same reasoning as before, nothing new to bind, and Start doing double duty as "confirm" while the keyboard has focus is harmless.

Since Select is now the keyboard-toggle modifier, the in-game menu moved to the Guide/Home button (client only; not present on every device) -- D-Pad Down works as a standalone Escape too, for devices without one.

## GLES-only devices (Libmali)

Teeworlds 0.7's renderer is desktop-OpenGL-only (fixed-function, no native GLES path), and it packs its tile atlas into a `GL_TEXTURE_3D` array purely as a memory-packing trick. Devices with a real desktop-GL driver (e.g. Panfrost) run it natively with no changes. Devices stuck on a GLES-only vendor blob (Libmali, the more common/default driver on most of these handhelds) get two things automatically, both gated by each CFW's own `libgl_*.txt` so they no-op on devices that don't need them:

- **[gl4es](https://github.com/ptitSeb/gl4es)** (`gl4es.$DEVICE_ARCH/`, `LICENSE-GL4ES.txt`) translates the desktop-GL calls down to GLES so the game gets a working context at all.
- A source patch (`src/teeworlds-patches/gles-plain-2d-tiles.patch`) drops the `GL_TEXTURE_3D` tile-array path entirely, since gl4es has no real 3D-texture implementation (confirmed from its own source -- `glTexImage3D` is a stub that silently drops the depth dimension). Tiles render from a plain 2D atlas with a computed UV subrect per tile index instead, the same technique the game already uses for sprites (`CRenderTools::SelectSprite`). Apply it against the vanilla `0.7.5` tag before building if you want that codepath fixed too; the shipped `teeworlds.aarch64` already has it baked in.

Real desktop-GL Vulkan-over-Mesa alternatives (virgl/llvmpipe/zink via Westonpack+Mesapack) were tried first and ruled out: every one crashed with an identical segfault inside the vendor `libmali.so.1`'s own `eglInitialize()`/XCB path, independent of which Mesa backend was selected -- a bug in the closed-source driver's windowing-surface code, not fixable from this side.

## Local / LAN play

"Vanilla (Server)" in the menu starts a dedicated server on the device (8 players, deathmatch on dm1, LAN-only by default, no master server registration). Anyone on the same network can connect to it from their own Teeworlds client at the device's IP, port 8303. Edit `teeworlds/server.cfg` to change the map, gametype, player count, or to set `sv_register 1` if internet-facing hosting is wanted instead.

Both server options show a splash screen (title art plus "Server Running" / "Bot Server Running") for as long as the server is up, since a dedicated server has no game window of its own otherwise. Start+Select quits it as usual.

## Singleplayer / bots

Vanilla Teeworlds has no offline mode or AI, it's pure PvP. "Bot Server" in the menu instead runs [nheir's bMod](https://github.com/nheir/teeworlds/tree/server_bot0.7), a community server mod that fills empty slots with simple AI bots (pathfinding, weapon prediction, no teamwork) so there's someone to shoot at without anyone else online. Connect to it the same way as the regular local server, from the client's Local tab or `localhost:8303`. Edit `teeworlds/server_bot.cfg` to change the bot count (`sv_bot_slots`) or map.

## Compile

[teeworlds.com's downloads page](https://www.teeworlds.com/?page=downloads) lists seven links for 0.7.5: Windows 32bit/64bit, Linux x86/x86_64, Mac OS X, and two source downloads (`teeworlds-0.7.5-src.zip` / `teeworlds-0.7.5-src.tar.gz`, identical contents, just zip vs tar.gz). **Grab one of the two Source links.** The five platform builds (Windows/Linux/macOS) are prebuilt desktop binaries for their own architectures (x86/x86_64, or Windows PE regardless of bitness) -- none of them are usable for building this port, which targets aarch64 Linux and has to be compiled from source either way. If a platform build happens to run on some handheld/emulation layer, that's unrelated to this distribution; this port's `teeworlds.aarch64` is a native ELF binary built from the source tree below, not a repackaged copy of any platform download.

Neither the source archive nor any platform build gets you the full map set this port ships, though -- both only carry the ~16 official maps (`datasrc/maps/` in source, `data/maps/` in a platform build; checked file counts, they're near-identical, ~660 files either way). This port's `teeworlds/data/maps/` has 1,504. The difference is `datasrc/maps` is a **git submodule** (`.gitmodules` -> [teeworlds/teeworlds-maps](https://github.com/teeworlds/teeworlds-maps)), and GitHub's release zip/tar.gz downloads never include submodule content, so it shows up empty-ish in every teeworlds.com download regardless of platform. To reproduce the full map set, `git clone --recurse-submodules` the actual repo (or populate `datasrc/maps` from `teeworlds-maps` by hand) instead of extracting the plain source archive.

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
