#!/usr/bin/env bash
set -euo pipefail
install_prefix="${1:-${HOME}/.local}"
if [[ "$install_prefix" != /* ]]; then
    printf '%s\n' 'Der Installationspfad muss absolut sein.' >&2
    exit 2
fi
rm -f -- "$install_prefix/bin/airctrl-desklet" "$install_prefix/bin/airctrl-backend" \
    "$install_prefix/share/applications/airctrl-desklet.desktop" \
    "$install_prefix/share/icons/hicolor/scalable/apps/airctrl-desklet.svg" \
    "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/airctrl-desklet.desktop" \
    "$install_prefix/share/licenses/airctrl-desklet/LICENSE"
printf '%s\n' 'AirControl entfernt. Persönliche Einstellungen bleiben erhalten.'
