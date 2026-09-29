#!/usr/bin/env bash
# Steam launch-option bridge for a VR game managed by Fluorine.
# Usage: steam-vr-launch.sh INSTANCE PROFILE EXECUTABLE [ignored Steam command]
set -euo pipefail

if (($# < 3)); then
  echo "Usage: $0 INSTANCE PROFILE EXECUTABLE" >&2
  exit 2
fi

instance=$1
profile=$2
executable=$3

if [[ ! -f "$instance/ModOrganizer.ini" || ! -d "$instance/profiles/$profile" ]]; then
  echo "Fluorine instance or profile not found: $instance / $profile" >&2
  exit 2
fi

if [[ -z ${SteamAppId:-} && -z ${STEAM_COMPAT_APP_ID:-} ]]; then
  echo "Launch this script from the game's Steam Play button." >&2
  exit 2
fi

# Forwarding to an already-open MO process would use that process's environment,
# losing the Steam launch session that OpenVR needs.
if pgrep -f '^/.*/ModOrganizer-core([[:space:]]|$)' >/dev/null; then
  echo "Close the running Fluorine window, then launch the game from Steam." >&2
  exit 2
fi

data_home=${XDG_DATA_HOME:-$HOME/.local/share}
launcher="$data_home/fluorine/bin/fluorine-manager"
if [[ ! -x "$launcher" ]]; then
  echo "Fluorine launcher not found: $launcher" >&2
  exit 2
fi

exec "$launcher" -i "$instance" -p "$profile" run -e "$executable"
