#!/usr/bin/env bash
# Download the pinned v0.1.0-beta archive; call the local installer.
# Requires a published Release. Never run on an already modified console.
set -euo pipefail
REPO="Sendew/EmulationStation-fcamod"
TAG="v0.1.0-beta"
ASSET="emulationstation-3d-r36tmax-arm64.tar.gz"
EXPECTED_SHA256="6c31ef047feda893bbf354ee564c7f8784400e807187777d3eae247ba5a95213"
die() { echo "ERRO: $*" >&2; exit 1; }
[[ $EUID -eq 0 ]] || die 'Execute com sudo.'
for cmd in curl sha256sum tar mktemp uname; do command -v "$cmd" >/dev/null || die "Comando ausente: $cmd"; done
[[ "$(uname -m)" == aarch64 ]] || die 'Versao ARM64 apenas.'
[[ -r /etc/os-release ]] || die 'Distribuicao nao identificada.'
. /etc/os-release
[[ "$ID" == debian && "$VERSION_ID" == 13 ]] || die 'Beta testada apenas em Debian 13 / dArkOS4Clone.'
[[ ! -e /etc/systemd/system/emulationstation.service.d/3d-preview.conf && ! -e /usr/bin/emulationstation/emulationstation-3d ]] || die 'Ja existe um mod 3D; nao sobrescrever.'
WORK="$(mktemp -d /tmp/es3d-install.XXXXXXXX)"
trap 'rm -rf "$WORK"' EXIT
URL="https://github.com/$REPO/releases/download/$TAG/$ASSET"
echo "A descarregar pacote $TAG..."
curl -fLsS --retry 2 --connect-timeout 15 --max-time 180 "$URL" -o "$WORK/$ASSET" || die 'Release indisponivel ou erro de rede.'
echo "$EXPECTED_SHA256  $WORK/$ASSET" | sha256sum -c - --status || die 'SHA-256 incorreto; abortado.'
[[ "$(tar -tzf "$WORK/$ASSET")" == 'emulationstation-3d' ]] || die 'Arquivo inesperado no pacote.'
tar -xzf "$WORK/$ASSET" -C "$WORK" --no-same-owner --no-same-permissions
[[ -f "$WORK/emulationstation-3d" && ! -L "$WORK/emulationstation-3d" ]] || die 'Executavel ausente.'
INSTALL_URL="https://raw.githubusercontent.com/$REPO/3d-preview/scripts/3d-preview/install.sh"
curl -fLsS --retry 2 --connect-timeout 15 --max-time 60 "$INSTALL_URL" -o "$WORK/install.sh" || die 'Instalador indisponivel.'
bash -n "$WORK/install.sh" || die 'Falha na sintaxe do instalador.'
bash "$WORK/install.sh" "$WORK/emulationstation-3d"
