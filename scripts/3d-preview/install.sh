#!/usr/bin/env bash
# EmulationStation 3D Preview — local-binary installer (experimental)
set -euo pipefail
BASE=/usr/bin/emulationstation
DROP=/etc/systemd/system/emulationstation.service.d/3d-preview.conf
SERVICE=emulationstation.service
STATE=/var/lib/es3d-preview
die() { echo "ERROR: $*" >&2; exit 1; }
[[ $EUID -eq 0 ]] || die "Run with sudo."
[[ $# -eq 1 && -f $1 ]] || die "Usage: sudo bash install.sh /path/to/compiled/emulationstation"
[[ $(uname -m) == aarch64 ]] || die "Only tested for aarch64 dArkOS4Clone builds."
[[ -f $BASE/emulationstation.sh && -x $BASE/emulationstation ]] || die "Original EmulationStation installation not found."
[[ ! -e $DROP && ! -e $BASE/emulationstation-3d.sh && ! -e $STATE/installed ]] || die "A 3D preview install or configuration already exists; aborting to avoid overwriting it."
[[ ! -e $BASE/emulationstation-3d ]] || die "A 3D binary already exists; preserve or migrate it before using this installer."
# Avoid conflict with other ExecStart overrides.
if systemctl cat "$SERVICE" | grep -q '/run/systemd/system/emulationstation.service.d/'; then
  die "A runtime systemd override exists; inspect it before installing."
fi
grep -Fq '"$esdir/emulationstation" "$@"' "$BASE/emulationstation.sh" || die "Original launcher format is not supported."
mkdir -p "$STATE" "$(dirname "$DROP")"
cp -a "$BASE/emulationstation.sh" "$STATE/original-launcher.sh"
install -m 755 "$1" "$BASE/emulationstation-3d"
sed 's|"$esdir/emulationstation" "$@"|"$esdir/emulationstation-3d" "$@"|' "$BASE/emulationstation.sh" > "$BASE/emulationstation-3d.sh"
chmod 755 "$BASE/emulationstation-3d.sh"
cat > "$DROP" <<'EOF'
[Service]
ExecStart=
ExecStart=/usr/bin/emulationstation/emulationstation-3d.sh
Restart=on-failure
EOF
touch "$STATE/installed"
systemctl daemon-reload
if ! systemctl restart "$SERVICE"; then
  echo "Startup failed; reverting service configuration." >&2
  rm -f "$DROP"
  systemctl daemon-reload
  systemctl restart "$SERVICE" || true
  exit 1
fi
echo "Installed EmulationStation 3D Preview. Original executable was not changed."
