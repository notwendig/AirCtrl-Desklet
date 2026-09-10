#!/usr/bin/env bash
set -euo pipefail
install_prefix="${1:-${HOME}/.local}"
if [[ "$install_prefix" != /* ]]; then
    printf '%s\n' 'Der Installationspfad muss absolut sein.' >&2
    exit 2
fi
if command -v systemctl >/dev/null; then
    systemctl --user disable --now airctrl-server.service 2>/dev/null || true
fi
rm -f -- "$install_prefix/bin/airctrl-desklet" "$install_prefix/bin/airctrl-backend" \
    "$install_prefix/bin/airctrl-server" "$install_prefix/bin/airctrl-client" \
    "$install_prefix/share/applications/airctrl-desklet.desktop" \
    "$install_prefix/share/icons/hicolor/scalable/apps/airctrl-desklet.svg" \
    "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/airctrl-desklet.desktop" \
    "$install_prefix/share/licenses/airctrl-desklet/LICENSE" \
    "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/airctrl-server.service"
if command -v systemctl >/dev/null; then systemctl --user daemon-reload 2>/dev/null || true; fi
printf '%s\n' 'AirControl entfernt. Persönliche Einstellungen bleiben erhalten.'
printf '%s\n' '/etc/airctrld.cfg wurde als Administratorkonfiguration nicht gelöscht.'
