# Teeworlds PortMaster port -- developer notes

Build/technical reference for anyone maintaining or rebuilding this port. Not shipped to players; see `README.md` for that. `testing_thread.txt` has the full chronological investigation log this was distilled from.

## Compile

[teeworlds.com's downloads page](https://www.teeworlds.com/?page=downloads) lists seven links for 0.7.5: Windows 32bit/64bit, Linux x86/x86_64, Mac OS X, and two source downloads (`teeworlds-0.7.5-src.zip` / `teeworlds-0.7.5-src.tar.gz`, identical contents, just zip vs tar.gz). **Grab one of the two Source links.** The five platform builds are prebuilt desktop binaries for their own architectures (x86/x86_64, or Windows PE regardless of bitness) -- none of them are usable for building this port, which targets aarch64 Linux and has to be compiled from source either way. This port's `teeworlds.aarch64` is a native ELF binary built from the source tree below, not a repackaged copy of any platform download.

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
