# Teeworlds PortMaster port -- developer notes

Build/technical reference for anyone maintaining or rebuilding this port. Not shipped to players; see `README.md` for that. `testing_thread.txt` has the full chronological investigation log this was distilled from.

## Compile

[teeworlds.com's downloads page](https://www.teeworlds.com/?page=downloads) lists seven links for 0.7.5: Windows 32bit/64bit, Linux x86/x86_64, Mac OS X, and two source downloads (`teeworlds-0.7.5-src.zip` / `teeworlds-0.7.5-src.tar.gz`, identical contents, just zip vs tar.gz). **Grab one of the two Source links.** The five platform builds are prebuilt desktop binaries for their own architectures (x86/x86_64, or Windows PE regardless of bitness) -- none of them are usable for building this port, which targets aarch64 Linux and has to be compiled from source either way. This port's `teeworlds.aarch64` is a native ELF binary built from the source tree below, not a repackaged copy of any platform download.

Neither the source archive nor any platform build gets you the full map set this port ships, though -- both only carry the ~16 official maps (`datasrc/maps/` in source, `data/maps/` in a platform build; checked file counts, they're near-identical, ~660 files either way). The difference is `datasrc/maps` is a **git submodule** (`.gitmodules` -> [teeworlds/teeworlds-maps](https://github.com/teeworlds/teeworlds-maps)), and GitHub's release zip/tar.gz downloads never include submodule content, so it shows up empty-ish in every teeworlds.com download regardless of platform. To pull the full community map set, `git clone --recurse-submodules` the actual repo (or populate `datasrc/maps` from `teeworlds-maps` by hand) instead of extracting the plain source archive -- that submodule has ~1,500 maps as of this writing.

This port's `teeworlds/data/maps/` ships 701, not the full ~1,500 -- see the Bot Server section below for why roughly half were pruned out (bot-engine pathfinding time on some of them was bad enough to be worth the smaller distribution). The 803 removed maps are kept, unpruned, at `Installer/Src/tools/discarded_maps/` if any specific one needs restoring; `mapscan_full.csv` in the same folder has vertex counts for all 1,504 original maps to help decide.

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

## GLES-only devices (Libmali)

Teeworlds 0.7's renderer is desktop-OpenGL-only (fixed-function, no native GLES path), and it packs its tile atlas into a `GL_TEXTURE_3D` array purely as a memory-packing trick. Devices with a real desktop-GL driver (e.g. Panfrost) run it natively with no changes. Devices stuck on a GLES-only vendor blob (Libmali, the more common/default driver on most of these handhelds) get two things automatically, both gated by each CFW's own `libgl_*.txt` so they no-op on devices that don't need them:

- **[gl4es](https://github.com/ptitSeb/gl4es)** (`gl4es.$DEVICE_ARCH/`, `LICENSE-GL4ES.txt`) translates the desktop-GL calls down to GLES so the game gets a working context at all.
- A source patch (`src/teeworlds-patches/gles-plain-2d-tiles.patch`) drops the `GL_TEXTURE_3D` tile-array path entirely, since gl4es has no real 3D-texture implementation (confirmed from its own source -- `glTexImage3D` is a stub that silently drops the depth dimension). Tiles render from a plain 2D atlas with a computed UV subrect per tile index instead, the same technique the game already uses for sprites (`CRenderTools::SelectSprite`). Apply it against the vanilla `0.7.5` tag before building if you want that codepath fixed too; the shipped `teeworlds.aarch64` already has it baked in.

Real desktop-GL Vulkan-over-Mesa alternatives (virgl/llvmpipe/zink via Westonpack+Mesapack) were tried first and ruled out: every one crashed with an identical segfault inside the vendor `libmali.so.1`'s own `eglInitialize()`/XCB path, independent of which Mesa backend was selected -- a bug in the closed-source driver's windowing-surface code, not fixable from this side.

## Controller tuning

`teeworlds/teeworlds.ini`'s `[config]` block controls gptokeyb2's stick-to-mouse aim emulation. Checked gptokeyb2's own source (`analog.c`/`config.c`) rather than assuming values from another port's config:

- `mouse_delay` is clamped to `[16, 3000]` in gptokeyb2's own config parser -- 16 is already the floor, no more responsiveness available from that knob.
- `deadzone_scale` is the real speed lever: `analog.c` multiplies the (deadzone-processed) stick vector directly into a per-tick pixel delta, re-emitted every `mouse_delay` ms. px/sec at full deflection = `deadzone_scale * (1000 / mouse_delay)`.
- `deadzone_mode = scaled_radial` gives a smooth linear ramp from the deadzone edge to full speed (`dz_scaled_radial` in `analog.c`), instead of `axial`'s hard independent per-axis cutoff.

A button carrying both `hold_state` (opens a sub-layer overlay while held) and its own keycode fires that keycode on *every* press, including presses only meant to reach the sub-layer (`state.c`: `emitKey()` runs unconditionally on press, independent of the `hold_state` push). This is why R2 and the L2 hotkey modifier have no bare action of their own in `teeworlds.ini` -- giving either one a standalone keypress would fire it every time the modifier is held to reach a combo underneath it.

## OmniOSK key remapping

OmniOSK's D-Pad navigation used to work for free, because the D-Pad sent plain arrow keys during normal gameplay (inert while alive, reusable for OSK nav with zero extra config). Now that the D-Pad drives real gameplay actions (movement/jump/menu), `Teeworlds.sh` sets `OMNI_UP_KEY`/`OMNI_DOWN_KEY`/`OMNI_LEFT_KEY`/`OMNI_RIGHT_KEY` explicitly to match whatever the D-Pad currently sends, and `OMNI_CONFIRM_KEY` is pinned to Start's existing "t" (chat) key rather than a dedicated button -- both configurable via env var, see `OmniOSK/src/config.c`. If the D-Pad bindings in `teeworlds.ini` change again, these four env vars need to move with them or OSK navigation breaks silently (wrong keys, no error).

## Map picker / offline play (Vanilla Server, Bot Server)

`teeworlds/map_picker.c` (source in this port, not upstream) is a standalone SDL2 tool, same build pattern as `menu_picker` (`stb_image.h` for PNG, no `SDL2_image`), plus FreeType for text (rasterized per-frame into an SDL texture, no glyph atlas or caching -- fine for a small static UI, would need one if this grows). `Teeworlds.sh`'s `pick_map()` runs it before either server mode starts, writing the chosen map name to a result file; `Teeworlds.sh` reads that back into `$PICKED_MAP` and passes `sv_map <name>` as a console-command argv to the server binary (Teeworlds' `ParseArguments` runs any plain CLI arg through the console parser, see `src/engine/shared/console.cpp`).

**Row/column layout is computed at runtime from the real window size**, not fixed constants -- confirmed on real hardware that a fixed 16-row/400px-column layout (sized for the X55's 1280x720) overflowed both edges on the R36S's 640x480 screen and pushed the hint bar off-screen entirely. `s_VisibleRows` is set once from `SDL_GetWindowSize()` right after `SDL_CreateWindow` (fullscreen-desktop, so it doesn't change mid-run); column width is `(WinW - margins - gaps) / NUM_COLUMNS`, clamped to a floor. If this UI ever needs to support window resize events, both would need recomputing on `SDL_WINDOWEVENT_RESIZED`, not just at startup.

**Map filenames are untrusted input feeding a console command.** `data/maps/` ships ~685 third-party community maps of unknown provenance, and the picked name is spliced directly into an argv string the server's own `ExecuteLineStroked` parses, where `;` is a command separator and `#` starts a comment. A map named e.g. `x;sv_register 1.map` would silently run a second console command at every launch. This build actually ships a map literally named `;).map`, confirmed live (harmless by luck -- it just produces an "invalid arguments"/"no such command" pair, since `)` isn't a real command). `pick_map()` in `Teeworlds.sh` blocks `PICKED_MAP` containing `;`, `#`, `"`, `\`, or a newline before it's used, falling back to whatever `sv_map` is already in the `.cfg` (same as if nothing were picked). Deliberately a deny-list, not an allow-list -- real community map names use plenty of punctuation (`!newmap`, `'Nso`, `1 (15)`) that a narrower filter would have silently mispicked instead of blocking.

**gptokeyb2 has no way to detect `map_picker` exiting on its own** (see the Controller tuning note above on `hold_state`/keycode interaction -- unrelated mechanism, same theme of gptokeyb2 quirks needing source verification, not assumption). It just reads the joystick and writes uinput forever once started. Confirmed on real hardware: a plain `kill` (SIGTERM) on the tracked PID left it running for an unpredictable, long stretch -- still alive after 1+ second, only ever reaped by PortMaster's own supervisor at full script exit, meanwhile its `left`/`right`/`y` = `tab` binds kept firing into the match that followed and spammed the scoreboard on every one of those button presses. Fixed with `pkill -9 -f "$GAMEDIR/map_picker.ini"` right after `map_picker` exits, which also sweeps up any earlier attempt's instance still lingering from the same PortMaster session.

**Cancel binding: Guide isn't reliably mapped.** Confirmed by diffing the live, ES-generated `/tmp/gamecontrollerdb.txt` (not the static repo `gamecontrollerdb.txt`, which has stale/alternate entries for the same device names) on both devices: the R36S's active `r36s_Gamepad` entry has `guide:b10`, but the X55's active `retrogame_joypad` entry (GUID `03009b4d...`) has no `guide:` mapping at all -- other GUID variants for the same device name in the static db do, but they aren't the one actually in use. `map_picker.ini` binds `guide = "esc"` as a bonus where it works, and `l2 = "esc"` as the reliable primary cancel (L2 was otherwise unused in this config). Worth re-checking if controllerdb entries change upstream.

**Bot Server's nav-mesh precompute time scales roughly cubically with the map's nav graph vertex count, and it's an algorithmic ceiling, not a hardware one.** Corner-finding/triangulation (which logs first, "Found N corners" / "Build N triangles") is fast regardless of map size -- the slow part is specifically the all-pairs closest-path/diameter computation that follows ("closest path computed, diameter=N" in the server's own log), almost certainly an O(V^3)-class algorithm in bMod's bot engine. Measured by scanning every one of the ~1,500 bundled maps for its vertex count (short-timeout runs, killed right after the fast "Graph with N vertices" line -- see `src/tools/mapscan.sh`/`calibrate.sh` if reproducing this), then full-timing a spread of real maps end-to-end on both devices:

| Map | Vertices | R36S (RK3326) | X55 (RK3566) |
|--|--|--|--|
| ctf1 | 240 | 1s | 1s |
| dm6 | 387 | 4s | 3s |
| dm2 (Official!) | 761 | 47s | 26s |
| DDavid.map | 856 | 75s | 37s |
| 1pVII | 1,631 | not ready at 180s | not ready at 180s |
| soki7 (largest bundled map, 3,669V) | 3,669 | not ready at 180s | not ready at 180s |

The X55 is consistently ~1.8-2x faster than the R36S at the same map (matches its generally stronger CPU), so better hardware does help in the mid-range -- but it doesn't rescue large maps: **both devices failed to become ready within 180s** for the two largest test maps. That ~2x-at-best hardware gap against what looks like cubic growth means no realistic device on this class of hardware will make a >1,500-vertex map viable for Bot Server. Vertex count distribution across the full bundled map set (1,139 of 1,504 maps returned a count within the scan's own short timeout; the other 365 didn't even finish the fast phase in time and are assumed high-risk by default): median 594V, p75 1,083V, p90 1,620V, max 3,669V -- so a large fraction of the "Others" column sits at or above the point where this becomes a real problem. Not all Official maps are exempt either -- `dm2` at 761V takes tens of seconds on its own.

`Teeworlds.sh`'s Bot Server case polls `.bot_srv.log` for the server's real "rcon password" startup banner (proof the slow step finished) instead of a fixed sleep, capped at 90s -- covers everything up to roughly the `DDavid.map` tier per the table above.

**Acted on directly rather than just documented:** this data became the basis for pruning the shipped "Others" map set (see the Compile section above) -- every non-Official map with either no vertex count within the scan's short timeout, or a count over 800V (chosen as the cutoff since `DDavid.map` at 856V/75s was already uncomfortably close to the 90s poll cap under real-world thermal conditions, while `dm2` at 761V/47s had real margin), got moved out of the shipped set. That's 803 of the original 1,504 maps, leaving 701 shipped (685 in the Others column + the 16 Official). The Official maps were kept regardless of their own vertex count, `dm2` included, since they're structurally referenced elsewhere (`kOfficialMaps` in `map_picker.c`, `votes.cfg`, `server.cfg`'s default `sv_map`) and can't just be dropped. The 803 removed maps themselves (~50MB) aren't in this repo, kept only in the local working copy as a recovery cache, not worth the repo bloat; `src/tools/mapscan_full.csv` has every original map's vertex count though, so the cutoff can be revisited (or a removed map's name identified) without re-running the multi-hour scan.
