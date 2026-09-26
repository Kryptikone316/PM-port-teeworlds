#!/bin/bash
# Scans every map in data/maps/ with the bot server, grabbing the nav-graph
# vertex/edge count (which prints fast, before the slow closest-path step)
# without waiting for that slow step to finish. timeout kills each attempt
# well before the slow part could even start on any but the largest maps.
GAMEDIR=/roms/ports/teeworlds
cd "$GAMEDIR" || exit 1
OUT="$1"
LIMIT="${2:-0}"   # 0 = all maps, else stop after N (for piloting)
: > "$OUT"

i=0
for f in data/maps/*.map; do
  name=$(basename "$f" .map)
  i=$((i+1))
  if [ "$LIMIT" -gt 0 ] && [ "$i" -gt "$LIMIT" ]; then
    break
  fi
  # Same deny-list as Teeworlds.sh's pick_map() -- defensive, this name is
  # about to hit the same console parser via -f/argv.
  case "$name" in
    *';'*|*'#'*|*'"'*|*'\'*)
      echo "${name}|SKIPPED|SKIPPED" >> "$OUT"
      continue
      ;;
  esac
  LOG="/tmp/mapscan_one.log"
  : > "$LOG"
  timeout 8 ./teeworlds_bot_srv.aarch64 -f server_bot.cfg "sv_map $name" > "$LOG" 2>&1
  V=$(grep -oP 'Graph with \K[0-9]+(?= vertices)' "$LOG" | head -1)
  E=$(grep -oP 'vertices and \K[0-9]+(?= edges)' "$LOG" | head -1)
  echo "${name}|${V:-NONE}|${E:-NONE}" >> "$OUT"
done
echo "DONE $i maps scanned"
