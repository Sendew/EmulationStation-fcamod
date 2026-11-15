#!/usr/bin/env bash
# EmulationStation 3D Preview: local executable installer (beta)
set -euo pipefail
BASE=/usr/bin/emulationstation
DROP=/etc/systemd/system/emulationstation.service.d/3d-preview.conf
STATE=/var/lib/es3d-preview
SERVICE=emulationstation.service
LAUNCHER="$BASE/emulationstation-3d.sh"
BINARY="$BASE/emulationstation-3d"
die() { echo "ERRO: $*" >&2; exit 1; }
[[ $EUID -eq 0 ]] || die 'Execute com sudo.'
[[ $# -eq 1 && -f "$1" && ! -L "$1" ]] || die 'Uso: sudo bash install.sh /caminho/para/emulationstation-3d'
[[ "$(uname -m)" == aarch64 ]] || die 'Esta beta foi testada apenas em ARM64.'
[[ -r /etc/os-release ]] || die 'Distribuicao nao identificada.'
. /etc/os-release
[[ "$ID" == debian && "$VERSION_ID" == 13 ]] || die 'Esta beta requer Debian 13 / dArkOS4Clone.'
[[ -f "$BASE/emulationstation.sh" && -x "$BASE/emulationstation" ]] || die 'EmulationStation original nao encontrado.'
[[ ! -e "$DROP" && ! -e "$LAUNCHER" && ! -e "$BINARY" && ! -e "$STATE" ]] || die 'Instalacao anterior detetada; nao sobrescrever.'
systemctl cat "$SERVICE" >/dev/null || die 'Servico emulationstation nao encontrado.'
[[ "$(systemctl show "$SERVICE" -p FragmentPath --value)" == /etc/systemd/system/emulationstation.service ]] || die 'Servico diferente do ambiente testado.'
if systemctl cat "$SERVICE" | grep -E '^# /(run|etc)/systemd/system/emulationstation.service.d/' >/dev/null; then
    die 'Ja existem overrides systemd. Inspecione antes de instalar.'
fi
grep -Fq '"$esdir/emulationstation" "$@"' "$BASE/emulationstation.sh" || die 'Launcher original nao reconhecido.'
[[ "$(od -An -tx1 -N4 "$1" | tr -d ' \n')" == 7f454c46 ]] || die 'O arquivo indicado nao e um executavel ELF.'
phase=0
rollback() {
    status=$?
    trap - EXIT
    if [[ $status -ne 0 && $phase -gt 0 ]]; then
        echo 'Erro durante instalacao: a restaurar inicializacao original...' >&2
        rm -f "$DROP"
        systemctl daemon-reload || true
        systemctl restart "$SERVICE" || true
        rm -f "$LAUNCHER" "$BINARY"
        rm -rf "$STATE"
    fi
    exit "$status"
}
trap rollback EXIT
mkdir -p "$STATE" "$(dirname "$DROP")"
phase=1
cp -a "$BASE/emulationstation.sh" "$STATE/original-launcher.sh"
install -m 755 "$1" "$BINARY"
sed 's|"$esdir/emulationstation" "$@"|"$esdir/emulationstation-3d" "$@"|' "$BASE/emulationstation.sh" > "$LAUNCHER"
chmod 755 "$LAUNCHER"
cat > "$DROP" <<'CONF'
[Service]
ExecStart=
ExecStart=/usr/bin/emulationstation/emulationstation-3d.sh
Restart=on-failure
CONF
systemctl daemon-reload
systemctl restart "$SERVICE"
sleep 2
systemctl is-active --quiet "$SERVICE" || die 'O servico 3D nao permaneceu ativo.'
touch "$STATE/installed"
phase=0
echo 'EmulationStation 3D ativo. Original preservado.'
