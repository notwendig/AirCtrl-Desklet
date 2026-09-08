#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
install_prefix="${1:-${HOME}/.local}"
if [[ "$install_prefix" != /* ]]; then
    printf '%s\n' 'Der Installationspfad muss absolut sein.' >&2
    exit 2
fi
cmake -S "$project_dir" -B "$project_dir/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install_prefix" -DBUILD_TESTING=OFF
cmake --build "$project_dir/build" --parallel --clean-first
pkill -x airctrl-backend 2>/dev/null || true
pkill -x airctrl-server 2>/dev/null || true
cmake --install "$project_dir/build"
# Absolute launcher works even when ~/.local/bin is not in Cinnamon's PATH.
python3 - "$install_prefix" <<'PY'
from pathlib import Path
import sys
prefix = Path(sys.argv[1])
binary = str(prefix / 'bin/airctrl-desklet')
if '\n' in binary or '\r' in binary:
    raise SystemExit('Zeilenumbruch im Installationspfad nicht unterstützt')
escaped = binary.replace('\\', '\\\\\\\\').replace('"', '\\\\"').replace('`', '\\\\`').replace('$', '\\\\$').replace('%', '%%')
desktop = prefix / 'share/applications/airctrl-desklet.desktop'
content = desktop.read_text()
desktop.write_text(content.replace('Exec=airctrl-desklet', 'Exec="' + escaped + '"'))
PY
service_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
service_file="$service_dir/airctrl-server.service"
mkdir -p -- "$service_dir"
python3 - "$install_prefix" "$service_file" <<'PY'
from pathlib import Path
import sys

binary = str(Path(sys.argv[1]) / 'bin/airctrl-server')
service = Path(sys.argv[2])
if any(c in binary for c in '\n\r'):
    raise SystemExit('Zeilenumbruch im Installationspfad nicht unterstützt')
escaped = binary.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%')
service.write_text(f'''[Unit]
Description=AirControl server for Philips AC2729
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart="{escaped}"
Restart=on-failure
RestartSec=10

[Install]
WantedBy=default.target
''')
PY
if [[ "${AIRCTRL_SKIP_SYSTEMD:-0}" != "1" ]] && command -v systemctl >/dev/null &&
   systemctl --user daemon-reload 2>/dev/null; then
    systemctl --user enable --now airctrl-server.service
else
    printf '%s\n' 'Hinweis: Kein systemd-Benutzerdienst; das Desklet startet den Server bei Bedarf.' >&2
fi
printf '%s\n' "Installiert. Start: $install_prefix/bin/airctrl-desklet"
printf '%s\n' "Server: $install_prefix/bin/airctrl-server"
printf '%s\n' "Client: $install_prefix/bin/airctrl-client status"
printf '%s\n' 'Autostart im Widget unter Einstellungen aktivieren.'
