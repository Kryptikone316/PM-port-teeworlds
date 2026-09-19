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
# 'a' (jump) already sends space -- reuse it as OmniOSK's confirm key instead
# of the default Return, so grid-select works without a dedicated button.
export OMNI_CONFIRM_KEY=space

PICKER_RESULT="$GAMEDIR/.picker_result"
rm -f "$PICKER_RESULT"

pm_platform_helper "$GAMEDIR/menu_picker.${DEVICE_ARCH}"
./menu_picker.${DEVICE_ARCH} loader_menu.png selector.png "$PICKER_RESULT"

CHOICE=$(cat "$PICKER_RESULT" 2>/dev/null)
CHOICE="${CHOICE:--1}"

case "$CHOICE" in
0)
  # Vanilla (Client)
  $GPTOKEYB2 "teeworlds" -c "$GAMEDIR/teeworlds.ini" &
  pm_platform_helper "$GAMEDIR/teeworlds.${DEVICE_ARCH}"
  LD_PRELOAD="$GAMEDIR/libs.${DEVICE_ARCH}/libomni_osk.so${LD_PRELOAD:+:$LD_PRELOAD}" ./teeworlds.${DEVICE_ARCH}
  ;;
1)
  # Vanilla (Server)
  $GPTOKEYB2 "teeworlds_srv" &
  SPLASH_PID=""
  if [ -f "$controlfolder/sdl2imgshow.${DEVICE_ARCH}" ]; then
    pm_platform_helper "$controlfolder/sdl2imgshow.${DEVICE_ARCH}"
    "$controlfolder/sdl2imgshow.${DEVICE_ARCH}" \
      -i "$GAMEDIR/splash.png" \
      -f "$GAMEDIR/data/fonts/DejaVuSans.ttf" \
      -s 32 -c "0,0,0" -t "Server Running" &
    SPLASH_PID=$!
  fi
  ./teeworlds_srv.${DEVICE_ARCH} -f server.cfg
  [ -n "$SPLASH_PID" ] && kill $SPLASH_PID 2>/dev/null
  ;;
2)
  # Bot Server
  $GPTOKEYB2 "teeworlds_bot" &
  SPLASH_PID=""
  if [ -f "$controlfolder/sdl2imgshow.${DEVICE_ARCH}" ]; then
    pm_platform_helper "$controlfolder/sdl2imgshow.${DEVICE_ARCH}"
    "$controlfolder/sdl2imgshow.${DEVICE_ARCH}" \
      -i "$GAMEDIR/splash.png" \
      -f "$GAMEDIR/data/fonts/DejaVuSans.ttf" \
      -s 32 -c "0,0,0" -t "Bot Server Running" &
    SPLASH_PID=$!
  fi
  ./teeworlds_bot_srv.${DEVICE_ARCH} -f server_bot.cfg
  [ -n "$SPLASH_PID" ] && kill $SPLASH_PID 2>/dev/null
  ;;
*)
  # Exit / cancelled out of the menu
  ;;
esac

pm_finish
