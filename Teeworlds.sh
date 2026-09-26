#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR=/$directory/ports/teeworlds

mkdir -p "$GAMEDIR/conf"
cd $GAMEDIR

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# Config, downloaded skins/maps, and screenshots default to $HOME/.teeworlds
export HOME="$GAMEDIR/conf"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export OMNI_MODE=buffered
export OMNI_EVENT_MODE=auto
# D-Pad no longer sends plain arrow keys (it's movement/jump/menu now, see
# teeworlds.ini), so OmniOSK's own nav can't rely on its arrow-key defaults
# any more -- point it at whatever the D-Pad actually sends instead. Confirm
# reuses Start's existing "t" (chat) rather than a dedicated button, same
# reasoning as the old space/jump reuse: nothing new to bind, and Start
# doing double duty as "confirm" while the keyboard has focus is harmless.
export OMNI_UP_KEY=space
export OMNI_DOWN_KEY=esc
export OMNI_LEFT_KEY=a
export OMNI_RIGHT_KEY=d
export OMNI_CONFIRM_KEY=t

# Teeworlds 0.7's SDL/OpenGL backend has no native GLES path (desktop GL only,
# see src/engine/client/backend_sdl.cpp) -- GLES-only Mali blobs (rk3326-class,
# e.g. R36S/dArkOS) hand it a context that "creates" fine but returns
# GL_MAX_TEXTURE_SIZE=0 and never renders a texture. Route through gl4es
# unconditionally so the same launch path works on both GLES-only and real-
# desktop-GL devices. Shared by every case below that launches the client.
setup_gl4es() {
  if [ -f "${controlfolder}/libgl_${CFW_NAME}.txt" ]; then
    source "${controlfolder}/libgl_${CFW_NAME}.txt"
  else
    source "${controlfolder}/libgl_default.txt"
  fi
  if [ "$LIBGL_FB" != "" ]; then
    # teeworlds.${DEVICE_ARCH} has a hard ELF DT_NEEDED on libGL.so.1, resolved
    # by the loader before SDL_VIDEO_GL_DRIVER ever comes into play. Some
    # libgl_*.txt copies don't prepend gl4es.$DEVICE_ARCH to LD_LIBRARY_PATH
    # themselves (confirmed on a real rk3326/ROCKNIX device, Sep 2026 build) --
    # set it explicitly so the loader picks up our real gl4es/libGL.so.1
    # instead of the system's GLES-only stub (a bind-mounted /dev/null on that
    # device).
    export LD_LIBRARY_PATH="$GAMEDIR/gl4es.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
    export SDL_VIDEO_GL_DRIVER="$GAMEDIR/gl4es.${DEVICE_ARCH}/libGL.so.1"
    export SDL_VIDEO_EGL_DRIVER="$GAMEDIR/gl4es.${DEVICE_ARCH}/libEGL.so.1"
  fi
}

# $1, if given, is an extra console command run at startup (e.g. "connect
# localhost:8303" for offline-vs-bots) -- ExecuteLine runs any plain
# command-line argument as a real console command (see
# src/engine/shared/console.cpp ParseArguments), no config-file needed.
run_client() {
  $GPTOKEYB2 "teeworlds" -c "$GAMEDIR/teeworlds.ini" &
  pm_platform_helper "$GAMEDIR/teeworlds.${DEVICE_ARCH}"
  LD_PRELOAD="$GAMEDIR/libs.${DEVICE_ARCH}/libomni_osk.so${LD_PRELOAD:+:$LD_PRELOAD}" ./teeworlds.${DEVICE_ARCH} "$@"
}

# Only used by the server-hosting cases below -- the online client keeps
# using Teeworlds' own in-game vote menu for map switching (see
# teeworlds/votes.cfg), no picker needed there. Sets $PICKED_MAP to the
# chosen map name, or empty if cancelled (caller falls back to whatever
# sv_map is already in the .cfg).
pick_map() {
  MAP_RESULT="$GAMEDIR/.map_picker_result"
  rm -f "$MAP_RESULT"

  $GPTOKEYB2 "map_picker" -c "$GAMEDIR/map_picker.ini" &
  pm_platform_helper "$GAMEDIR/map_picker.${DEVICE_ARCH}"
  # map_picker's own Up/Down/Left/Right are real arrow keys already (unlike
  # the main client's remapped D-Pad), matching OmniOSK's defaults -- only
  # the confirm key needs overriding, off Return (which map_picker treats
  # as "pick this map") onto something neither tool otherwise uses.
  env OMNI_UP_KEY= OMNI_DOWN_KEY= OMNI_LEFT_KEY= OMNI_RIGHT_KEY= OMNI_CONFIRM_KEY=g \
    LD_PRELOAD="$GAMEDIR/libs.${DEVICE_ARCH}/libomni_osk.so${LD_PRELOAD:+:$LD_PRELOAD}" \
    ./map_picker.${DEVICE_ARCH} "$GAMEDIR/data/maps" "$GAMEDIR/conf/favorites.txt" \
    "$GAMEDIR/data/fonts/DejaVuSans.ttf" "$GAMEDIR/map_picker_bg.png" "$MAP_RESULT"

  # gptokeyb2 has no way to notice map_picker exiting on its own (it just
  # reads the joystick and writes uinput forever) -- left alive, its
  # left/right/y="tab" binds keep firing during the match that follows,
  # spamming the scoreboard every time those buttons are pressed in-game.
  # Plain SIGTERM (via a tracked PID + wait) was confirmed on real hardware
  # to leave it running for a long, unpredictable stretch -- pkill -9 by
  # its unique config path instead, which also sweeps up any earlier
  # attempt's instance still lingering from the same PortMaster session.
  pkill -9 -f "$GAMEDIR/map_picker.ini" 2>/dev/null

  PICKED_MAP=$(cat "$MAP_RESULT" 2>/dev/null)

  # PICKED_MAP came straight off a filename in data/maps/ (~685 bundled
  # third-party maps, unvetted) and is about to become a literal argv word
  # the server engine feeds through its own console parser, where ';' is a
  # command separator and '#' starts a comment (src/engine/shared/console.cpp
  # ExecuteLineStroked) -- a map named e.g. "x;sv_register 1.map" would
  # silently inject a second console command at every launch. Confirmed
  # live: this build actually ships a map literally named ";).map". Block
  # just the characters that are actually dangerous to the console parser
  # (';' '#' '"' '\' and newline) rather than an allow-list -- real map
  # names use plenty of punctuation ("!newmap", "'Nso", "1 (15)") that a
  # narrower filter would have silently mispicked instead of blocking.
  if [[ "$PICKED_MAP" == *[\;\#\"\\]* ]] || [[ "$PICKED_MAP" == *$'\n'* ]]; then
    PICKED_MAP=""
  fi
}

PICKER_RESULT="$GAMEDIR/.picker_result"
rm -f "$PICKER_RESULT"

pm_platform_helper "$GAMEDIR/menu_picker.${DEVICE_ARCH}"
./menu_picker.${DEVICE_ARCH} loader_menu.png selector.png "$PICKER_RESULT"

CHOICE=$(cat "$PICKER_RESULT" 2>/dev/null)
CHOICE="${CHOICE:--1}"

case "$CHOICE" in
0)
  # Vanilla (Client)
  setup_gl4es
  run_client
  ;;
1)
  # Vanilla (Server)
  # Hosts a real dedicated server in the background and joins it with the
  # client automatically -- still joinable by others over LAN/internet
  # (see server.cfg for sv_register) exactly as before, but the host isn't
  # just staring at a splash screen any more, they get to play too.
  pick_map
  ./teeworlds_srv.${DEVICE_ARCH} -f server.cfg ${PICKED_MAP:+"sv_map $PICKED_MAP"} &
  SRV_PID=$!
  # Fixed short delay rather than a real readiness poll -- the server binds
  # its port well within this, and polling would need extra tooling
  # (nc/ss) that isn't guaranteed present on every CFW.
  sleep 1.5

  setup_gl4es
  run_client "connect localhost:8303"

  kill $SRV_PID 2>/dev/null
  ;;
2)
  # Bot Server
  # Same idea, using nheir's bMod bot server instead of vanilla -- fills
  # empty slots with simple AI bots, so this doubles as real single-device
  # offline play: no second device or even Wi-Fi needed (127.0.0.1 is
  # always up regardless of network state), but it's still a real joinable
  # dedicated server for anyone else on the LAN too.
  pick_map
  rm -f "$GAMEDIR/.bot_srv.log"
  ./teeworlds_bot_srv.${DEVICE_ARCH} -f server_bot.cfg ${PICKED_MAP:+"sv_map $PICKED_MAP"} > >(tee "$GAMEDIR/.bot_srv.log") 2>&1 &
  SRV_PID=$!
  # Unlike the vanilla server, bMod builds a bot pathfinding nav mesh before
  # it can accept connections -- corner-finding/triangulation is fast, but
  # the all-pairs closest-path step that follows scales roughly cubically
  # with the nav graph's vertex count. Measured on real hardware: dm1 (258
  # vertices) finishes inside a second; a mid-size "Others" pick, DDavid.map
  # (856 vertices), took 47s; an oversized one, !newmap, didn't finish in
  # 30s at all. A fixed short sleep or even the earlier 30s cap made the
  # client give up ("Unable to connect") long before the server was ever
  # listening. Poll for the server's real startup banner instead of
  # guessing a fixed delay; still capped so a truly pathological map can't
  # hang the launch forever -- 90s covers every Official/Favorites map and
  # most "Others" picks, per the numbers above, but an unusually large
  # community map can still exceed it (see DEV_NOTES.md).
  for i in $(seq 1 90); do
    grep -q "rcon password" "$GAMEDIR/.bot_srv.log" 2>/dev/null && break
    sleep 1
  done

  setup_gl4es
  run_client "connect localhost:8303"

  kill $SRV_PID 2>/dev/null
  ;;
*)
  # Exit / cancelled out of the menu
  ;;
esac

pm_finish
