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
printf '%s\n' "Installiert. Start: $install_prefix/bin/airctrl-desklet"
printf '%s\n' 'Autostart im Widget unter Einstellungen aktivieren.'
