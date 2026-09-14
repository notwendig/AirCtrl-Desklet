#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    printf '%s\n' \
        'Aufruf: bash install.sh [-s|--server] [-c|--client] [--prefix PFAD]' \
        '' \
        '  -s, --server       Nur Server, Konfiguration und Benutzerdienst installieren' \
        '  -c, --client       Nur Desklet und Kommandozeilen-Client installieren' \
        '      --prefix PFAD  Präfix überschreiben (Server: /usr/local; Client: ~/.local)' \
        '  -h, --help         Diese Hilfe anzeigen' \
        '' \
        'Ohne Rollenoption werden Server und Client installiert. -s und -c dürfen' \
        'gemeinsam angegeben werden. Ein einzelner absoluter PFAD bleibt kompatibel.'
}

usage_error() {
    printf 'FEHLER: %s\n\n' "$1" >&2
    usage >&2
    exit 2
}

install_server=0
install_client=0
selection_seen=0
prefix_seen=0
prefix_override=""

while (($# > 0)); do
    case "$1" in
        -s|--server)
            install_server=1
            selection_seen=1
            ;;
        -c|--client)
            install_client=1
            selection_seen=1
            ;;
        --prefix)
            shift
            (($# > 0)) || usage_error 'Nach --prefix fehlt der Installationspfad.'
            ((prefix_seen == 0)) || usage_error 'Der Installationspfad wurde mehrfach angegeben.'
            prefix_override="$1"
            prefix_seen=1
            ;;
        --prefix=*)
            ((prefix_seen == 0)) || usage_error 'Der Installationspfad wurde mehrfach angegeben.'
            prefix_override="${1#*=}"
            [[ -n "$prefix_override" ]] || usage_error 'Nach --prefix= fehlt der Installationspfad.'
            prefix_seen=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            (($# <= 1)) || usage_error 'Nach -- ist höchstens ein Installationspfad erlaubt.'
            if (($# == 1)); then
                ((prefix_seen == 0)) || usage_error 'Der Installationspfad wurde mehrfach angegeben.'
                prefix_override="$1"
                prefix_seen=1
            fi
            break
            ;;
        -*)
            usage_error "Unbekannte Option: $1"
            ;;
        *)
            ((prefix_seen == 0)) || usage_error 'Der Installationspfad wurde mehrfach angegeben.'
            prefix_override="$1"
            prefix_seen=1
            ;;
    esac
    shift
done

if ((selection_seen == 0)); then
    install_server=1
    install_client=1
fi

if ((prefix_seen)) && [[ "$prefix_override" != /* ]]; then
    usage_error 'Der Installationspfad muss absolut sein.'
fi

if ((prefix_seen)); then
    server_prefix="$prefix_override"
    client_prefix="$prefix_override"
else
    server_prefix="/usr/local"
    client_prefix="${HOME}/.local"
fi
server_build="$project_dir/build/RELEASE/server"
client_build="$project_dir/build/RELEASE/client"

if ((install_server)); then
    cmake -S "$project_dir" -B "$server_build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DAIRCTRL_COMPONENT=server \
        -DCMAKE_INSTALL_PREFIX="$server_prefix" -DBUILD_TESTING=OFF \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build "$server_build" --parallel --clean-first
fi
if ((install_client)); then
    cmake -S "$project_dir" -B "$client_build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DAIRCTRL_COMPONENT=client \
        -DCMAKE_INSTALL_PREFIX="$client_prefix" -DBUILD_TESTING=OFF \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build "$client_build" --parallel --clean-first
fi

# Give clangd the exact generated-header and dependency include paths. It only
# searches parent directories of a source file, not nested CMake build trees.
python3 - "$project_dir" "$install_server" "$server_build" \
    "$install_client" "$client_build" <<'PY'
import json
from pathlib import Path
import sys

project = Path(sys.argv[1])
selected = ((sys.argv[2] == '1', Path(sys.argv[3])),
            (sys.argv[4] == '1', Path(sys.argv[5])))
commands = []
seen = set()
for enabled, build in selected:
    if not enabled:
        continue
    database = build / 'compile_commands.json'
    try:
        entries = json.loads(database.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f'FEHLER: CMake-Kompilierungsdatenbank fehlt oder ist ungültig: {database}: {error}')
    if not isinstance(entries, list):
        raise SystemExit(f'FEHLER: Ungültige CMake-Kompilierungsdatenbank: {database}')
    for entry in entries:
        source = entry.get('file') if isinstance(entry, dict) else None
        if not isinstance(source, str) or source in seen:
            continue
        seen.add(source)
        commands.append(entry)

target = project / 'compile_commands.json'
temporary = project / 'compile_commands.json.tmp'
temporary.write_text(json.dumps(commands, indent=2) + '\n')
temporary.replace(target)
PY

prefix_is_writable() {
    local candidate="$1"
    while [[ ! -e "$candidate" ]]; do
        candidate="${candidate%/*}"
        [[ -n "$candidate" ]] || candidate="/"
    done
    [[ -d "$candidate" && -w "$candidate" ]]
}

install_component() {
    local build="$1"
    local prefix="$2"
    if [[ ${EUID} -eq 0 ]] || prefix_is_writable "$prefix"; then
        cmake --install "$build"
    elif command -v sudo >/dev/null; then
        sudo cmake --install "$build"
    else
        printf 'FEHLER: Für die Installation unter %s werden Root-Rechte benötigt; sudo fehlt.\n' \
            "$prefix" >&2
        exit 1
    fi
}

if ((install_server)); then
    pkill -x airctrl-server 2>/dev/null || true
    install_component "$server_build" "$server_prefix"
    # Remove the obsolete single-image plotter from earlier v1.06 packages.
    obsolete_plotter="$server_prefix/share/airctrl-desklet/plot-airctrl.gnuplot"
    if [[ -e "$obsolete_plotter" || -L "$obsolete_plotter" ]]; then
        if [[ ${EUID} -eq 0 ]] || [[ -w "${obsolete_plotter%/*}" ]]; then
            rm -f -- "$obsolete_plotter"
        elif command -v sudo >/dev/null; then
            sudo rm -f -- "$obsolete_plotter"
        else
            printf 'WARNUNG: Veraltete Datei konnte nicht entfernt werden: %s\n' \
                "$obsolete_plotter" >&2
        fi
    fi
fi
if ((install_client)); then
    pkill -x airctrl-backend 2>/dev/null || true
    install_component "$client_build" "$client_prefix"
fi

# The daemon owns all device parameters. Preserve an existing administrator
# configuration; create the system file only on the first v1.06 installation.
if ((install_server)) && [[ "${AIRCTRL_SKIP_SYSTEM_CONFIG:-0}" != "1" ]]; then
    if [[ ! -e /etc/airctrld.cfg ]]; then
        if [[ ${EUID} -eq 0 ]]; then
            install -m 0644 -- "$project_dir/config/airctrld.cfg" /etc/airctrld.cfg
        elif command -v sudo >/dev/null; then
            sudo install -m 0644 -- "$project_dir/config/airctrld.cfg" /etc/airctrld.cfg
        else
            printf '%s\n' 'FEHLER: /etc/airctrld.cfg fehlt und sudo ist nicht verfügbar.' >&2
            exit 1
        fi
    else
        printf '%s\n' 'Vorhandene /etc/airctrld.cfg bleibt unverändert.'
    fi
    "$server_prefix/bin/airctrl-server" --config /etc/airctrld.cfg --check-config
fi

# The server is a user service, so its root-owned /var/log target must remain
# writable by exactly the user who runs this installer. Existing content is
# preserved. copytruncate lets logrotate retain that ownership and CSV schema.
if ((install_server)) && [[ "${AIRCTRL_SKIP_SYSTEM_LOG:-0}" != "1" ]]; then
    status_log=/var/log/airctrl.log
    if [[ -L "$status_log" || ( -e "$status_log" && ! -f "$status_log" ) ]]; then
        printf 'FEHLER: %s ist keine reguläre Datei.\n' "$status_log" >&2
        exit 1
    fi
    log_uid="$(id -u)"
    log_gid="$(id -g)"
    if [[ ${EUID} -eq 0 ]]; then
        touch -- "$status_log"
        chown -- "$log_uid:$log_gid" "$status_log"
        chmod 0640 -- "$status_log"
        install -d -m 0755 -- /etc/logrotate.d
        install -m 0644 -- "$project_dir/config/airctrl.logrotate" /etc/logrotate.d/airctrl
    elif command -v sudo >/dev/null; then
        sudo touch -- "$status_log"
        sudo chown -- "$log_uid:$log_gid" "$status_log"
        sudo chmod 0640 -- "$status_log"
        sudo install -d -m 0755 -- /etc/logrotate.d
        sudo install -m 0644 -- "$project_dir/config/airctrl.logrotate" /etc/logrotate.d/airctrl
    else
        printf '%s\n' 'FEHLER: Für /var/log/airctrl.log werden Root-Rechte benötigt; sudo fehlt.' >&2
        exit 1
    fi
fi
# Absolute launcher works even when ~/.local/bin is not in Cinnamon's PATH.
if ((install_client)); then
    python3 - "$client_prefix" <<'PY'
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
fi

if ((install_server)); then
    service_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
    service_file="$service_dir/airctrl-server.service"
    mkdir -p -- "$service_dir"
    python3 - "$server_prefix" "$service_file" <<'PY'
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
ExecStart="{escaped}" --config /etc/airctrld.cfg
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
        printf '%s\n' 'Hinweis: Kein systemd-Benutzerdienst eingerichtet; airctrl-server muss separat gestartet werden.' >&2
    fi
fi

printf '%s\n' 'Installation abgeschlossen.'
if ((install_server)); then
    printf '%s\n' "Server: $server_prefix/bin/airctrl-server"
    printf '%s\n' 'Serverkonfiguration: /etc/airctrld.cfg'
    printf '%s\n' 'Statusprotokoll: /var/log/airctrl.log'
    printf '%s\n' "PDF: $server_prefix/bin/airctrl-plot /var/log/airctrl.log \$HOME/airctrl-status.pdf"
    if ! python3 -c 'import matplotlib' >/dev/null 2>&1; then
        printf '%s\n' \
            'Hinweis: Für PDF-Diagramme fehlt Matplotlib (Fedora: sudo dnf install python3-matplotlib).' >&2
    fi
fi
if ((install_client)); then
    printf '%s\n' "Desklet: $client_prefix/bin/airctrl-desklet"
    printf '%s\n' "Client: $client_prefix/bin/airctrl-client status"
    printf '%s\n' 'Autostart im Widget unter Einstellungen aktivieren.'
fi
