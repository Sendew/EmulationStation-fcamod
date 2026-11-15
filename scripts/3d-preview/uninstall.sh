#!/usr/bin/env bash
# EmulationStation 3D Preview — uninstall only files managed by install.sh
set -euo pipefail
BASE=/usr/bin/emulationstation
DROP=/etc/systemd/system/emulationstation.service.d/3d-preview.conf
STATE=/var/lib/es3d-preview
[[ $EUID -eq 0 ]] || { echo "Run with sudo." >&2; exit 1; }
[[ -e $STATE/installed ]] || { echo "No managed installation found; refusing to remove a manually configured mod." >&2; exit 1; }
[[ -f $DROP ]] || { echo "Managed service override is missing; inspect before uninstalling." >&2; exit 1; }
grep -Fq 'ExecStart=/usr/bin/emulationstation/emulationstation-3d.sh' "$DROP" || { echo "Service override has been changed; inspect manually." >&2; exit 1; }
rm -f "$DROP"
systemctl daemon-reload
systemctl restart emulationstation.service
rm -f "$BASE/emulationstation-3d.sh" "$BASE/emulationstation-3d" "$STATE/installed"
echo "3D mod removed; original EmulationStation launcher and binary remain intact."
