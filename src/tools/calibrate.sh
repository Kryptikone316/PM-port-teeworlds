#!/bin/bash
# Full-timing calibration: for each named map, launch the real bot server
# and time how long until "rcon password" (true ready signal) appears,
# capped generously so this doesn't hang forever on the biggest ones.
GAMEDIR=/roms/ports/teeworlds
cd "$GAMEDIR" || exit 1
OUT="$1"
: > "$OUT"
shift
for name in "$@"; do
  LOG="/tmp/calib_one.log"
  : > "$LOG"
  START=$(date +%s)
  # `timeout` forks the real server as a CHILD and keeps its own PID as $!
  # (unlike `env`, which exec-replaces) -- killing $! only kills timeout's
  # wrapper, leaving the actual server running and holding port 8303,
  # confirmed on real hardware (broke every map after the first one this
  # way). pkill by the map-specific command line instead, so it hits the
  # real process regardless of any such indirection.
  timeout 180 ./teeworlds_bot_srv.aarch64 -f server_bot.cfg "sv_map $name" > "$LOG" 2>&1 &
  READY=0
  for i in $(seq 1 180); do
    if grep -q "rcon password" "$LOG" 2>/dev/null; then
      READY=1
      break
    fi
    if ! pgrep -f "teeworlds_bot_srv.aarch64.*sv_map $name\$" > /dev/null; then
      break
    fi
    sleep 1
  done
  END=$(date +%s)
  pkill -9 -f "teeworlds_bot_srv.aarch64.*sv_map $name\$" 2>/dev/null
  sleep 1
  ELAPSED=$((END-START))
  V=$(grep -oP 'Graph with \K[0-9]+(?= vertices)' "$LOG" | head -1)
  echo "${name}|${V:-NONE}|${ELAPSED}|${READY}" | tee -a "$OUT"
done
echo "CALIBRATION DONE"
