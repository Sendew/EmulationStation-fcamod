#!/usr/bin/env bash
# EmulationStation 3D Preview — update from a locally compiled binary
set -euo pipefail
BASE=/usr/bin/emulationstation
STATE=/var/lib/es3d-preview
[[ $EUID -eq 0 ]] || { echo "Run with sudo." >&2; exit 1; }
[[ $# -eq 1 && -f $1 ]] || { echo "Usage: sudo bash update.sh /path/to/new/emulationstation" >&2; exit 1; }
[[ -e $STATE/installed && -x $BASE/emulationstation-3d ]] || { echo "Not installed with install.sh." >&2; exit 1; }
cp -a "$BASE/emulationstation-3d" "$STATE/emulationstation-3d.previous"
install -m 755 "$1" "$BASE/emulationstation-3d.new"
mv -f "$BASE/emulationstation-3d.new" "$BASE/emulationstation-3d"
if ! systemctl restart emulationstation.service; then
  cp -a "$STATE/emulationstation-3d.previous" "$BASE/emulationstation-3d"
  systemctl restart emulationstation.service || true
  echo "Update failed; restored previous binary." >&2
  exit 1
fi
echo "Updated. Previous binary saved in $STATE."
