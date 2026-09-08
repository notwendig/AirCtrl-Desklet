# Entwicklung und Tests

[Deutsch](../README.md) · [English overview](../README.en.md) · [Architektur](ARCHITECTURE.md)

## Voraussetzungen

Linux, C++17-Compiler, CMake ab 3.16, Qt ab 6.2 (Core, Gui, Widgets, DBus, Network; zusätzlich
Test für Tests), OpenSSL Crypto, nlohmann/json ab 3.9 und Threads.
Die optionalen CMake-Presets benötigen **CMake ab 3.21** und Ninja.
Die Repository-/Paketprüfungen verwenden Python ab 3.9 und nur die Standardbibliothek.
Lua 5.4.9 wird aus `third_party/lua` statisch gebaut; ein systemweites
`lua-devel`/`liblua-dev` ist nicht erforderlich. Der Build benötigt deshalb
neben dem C++- auch einen C-Compiler.

Fedora:

```bash
sudo dnf install -y gcc-c++ cmake ninja-build make qt6-qtbase-devel qt6-qtsvg \
  openssl-devel json-devel python3 dejavu-sans-fonts
```

Ubuntu 24.04:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build qt6-base-dev \
  qt6-svg-dev libssl-dev nlohmann-json3-dev python3 fonts-dejavu-core
```

Wayland braucht außerdem die passenden Qt-Plattformpakete der Distribution.
Erzwinge `QT_QPA_PLATFORM=wayland` nicht ohne laufende Wayland-Sitzung. Dass das
Plugin installiert ist, bedeutet nicht, dass ein Wayland-Display verfügbar ist.

## Build mit Presets

Alle Befehle im Projektverzeichnis ausführen:

```bash
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev
./build/dev/airctrl-desklet --demo
```

`dev` ist Debug mit Tests; `release` ist Release ohne Tests; `ci` ist Release mit
Tests. Die Buildverzeichnisse sind voneinander getrennt. Lokale SDK-Pfade gehören
in ein nicht eingechecktes `CMakeUserPresets.json` oder in `-DCMAKE_PREFIX_PATH=…`.
Die Versionsnummer des Widgets stammt aus `project(... VERSION ...)` in der
obersten CMake-Datei. `airctrl-server` ist der einzige installierte Nutzer der
internen Philips-CoAP-Bibliothek; `airctrl-client` und das Desklet sprechen IPC.
Die Editorvorlage wird beim Konfigurieren aus `examples/automation.lua` in den
generierten Header `airctrl_automation_example.hpp` übernommen; diese generierte
Datei nicht manuell bearbeiten.

Klassisch, ohne Presets/Ninja:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Qt Creator: `CMakeLists.txt` öffnen, ein Qt-6-Desktop-Kit auswählen und für erste
GUI-Versuche `--demo` als Programmargument setzen.

## Testgrenzen

CTest startet QtTest mit `-platform offscreen`. Fake-Server und lokaler
UDP-Simulator ersetzen das Gerät. Der UDP-Test verwendet nur `127.0.0.1` und
einen dynamischen Port; keine IP des echten Geräts wird getestet.
`automation-tests` prüft die Lua-Sandbox, die bytegleiche Beispielvorlage,
Zeitpläne, Wochentage, Nachholen, Ereignisdaten, erlaubte Steuerfelder und das
Ausführungslimit ohne Gerätezugriff. Die Desklet-Suite prüft zusätzlich, dass
zwei Clients denselben Server, UDP-Port und Statusstrom verwenden und IPv4,
IPv6 sowie Hostnamen unverändert beim Server ankommen. Kann die isolierte
Umgebung keine Unix-Sockets anlegen, wird diese Suite ausdrücklich übersprungen;
das ist kein bestandener nativer IPC-Test. Für die vollständige Desklet-/Server-
Suite gilt ein CTest-Limit von 300 Sekunden, weil sie absichtlich einen
19-Sekunden-Datenpausentest und zahlreiche getrennte Serverstarts enthält.
Abgekoppelte Testserver leiten ihre Ausgabe nicht in CTest und beenden sich im
Testmodus auch ohne erste Clientverbindung. Synthetische Alarmfälle erreichen
nur bei der ausdrücklich privaten D-Bus-Prüfung einen Benachrichtigungsdienst,
niemals die normale Desktopsitzung des Entwicklers.

Die native Fensterverwaltung, Clipboard-/Popup-Verhalten, Tray und Autostart
müssen zusätzlich interaktiv geprüft werden. Simuliertes Wayland-Routing ist
kein Test unter einem echten Compositor. Ausgelassene Tests sind kein Erfolg.
Der konkrete geprüfte Stand steht in [VALIDATION.md](../VALIDATION.md).
Die reale v1.04-Paketprüfung ist getrennt unter
[PROTOCOL_VALIDATION_2026-09-08.md](PROTOCOL_VALIDATION_2026-09-08.md)
dokumentiert. Sie ist ein externer Hardwarebefund und ersetzt weder den
Loopback-Test noch eine reproduzierbare Gerätesimulation. Rohmitschnitte bleiben
privat und werden vom Repositoryprüfer als öffentliche Quelldatei abgelehnt.

Optional kann der D-Bus-Test in einer isolierten Testsitzung laufen:

```bash
cmake --preset ci -DAIRCTRL_TEST_WITH_DBUS=ON
cmake --build --preset ci --parallel 2
ctest --preset ci
```

Dafür muss `dbus-run-session` installiert und das Anlegen eines lokalen
Testsockets erlaubt sein. Bei einer Berechtigungssperre nicht auf die echte
Benutzersitzung ausweichen. Der normale Testbetrieb lässt diese Prüfung aus.

Repository- und Paketprüfungen:

```bash
python3 scripts/check_repository.py
python3 -m unittest discover -s tests -p 'test_repository.py' -v
python3 scripts/package_source.py
```

Das Paketprogramm erstellt einen Quell-ZIP mit SHA-256-Datei unter `dist/`.
Es benutzt eine Positivliste, verbietet Symlinks und prüft öffentliche Dateien.
Builds, Git-Metadaten, SDKs, Einstellungen und Mitschnitte werden nicht verpackt.
Bereits vorhandene Ausgabedateien werden nicht überschrieben. Für einen erneuten
Versuch einen anderen Ausgabepfad mit `--output /absoluter/pfad/datei.zip` wählen.
Die reproduzierbare ZIP-Struktur ersetzt keine signierte Herkunftsbestätigung.
Das Übergabe-ZIP kann zusätzlich ein versionsbezogenes Einspielskript enthalten. Dieses Skript
prüft ein sauberes vorhandenes Git-Arbeitsverzeichnis, kopiert nur die durch die
Repository-Positivliste freigegebenen Dateien, baut und testet, installiert
unter `~/.local` und erstellt danach Commit und annotierten Tag. Der abschließende
Push erfolgt atomar über den ausdrücklich gesetzten SSH-Remote und niemals mit
`--force`. Das Skript ist kein CI-Ersatz; der GitHub-Lauf beginnt erst nach dem Push.

## Server und Clients lokal prüfen

```bash
./build/dev/airctrl-server
./build/dev/airctrl-client server-status
./build/dev/airctrl-client status
./build/dev/airctrl-client watch
./build/dev/airctrl-client set pwr=1
```

`airctrl-server` bleibt beim Schließen eines Clients aktiv. `airctrl-client refresh`
erneuert nur den Geräteclient samt UDP-Socket; der Unix-Socket bleibt bestehen.
Das Nachrichtenformat steht in [IPC_PROTOCOL.md](IPC_PROTOCOL.md).

## Vorschauen ohne Gerät

```bash
QT_QPA_PLATFORM=offscreen ./build/dev/airctrl-desklet --screenshot /tmp/airctrl-demo.png
QT_QPA_PLATFORM=offscreen ./build/dev/diagnostics-preview /tmp/airctrl-diagnostics.png
```

`--screenshot` verwendet Demo-Werte. `diagnostics-preview` wird nur mit Tests
gebaut und rendert den echten Diagnosedialog mit fest eingebauten, synthetischen
Gerätedaten. Es greift auf kein Gerät zu und überschreibt keine vorhandene PNG.
Einzelne QtTest-Vorschauen lassen sich
über `AIRCTRL_TEST_DIAGNOSTICS_PNG`, `AIRCTRL_TEST_ALARMS_PNG`,
`AIRCTRL_TEST_EMBLEMS_PNG`, `AIRCTRL_TEST_FILTERS_PNG` und
`AIRCTRL_TEST_POWER_PNG` auf gewünschte PNG-Pfade schreiben.
Die öffentlichen Diagnosebilder müssen weiterhin frei von echten Kennungen sein.

## CI und Releases

Die vorbereitete [CI](../.github/workflows/ci.yml) baut auf Ubuntu 24.04 mit GCC
und Clang sowie in einem Fedora-44-Container mit GCC. Sie prüft Repository,
Python-Pakettests, C++-Build, QtTest, Installation in einen temporären Präfix,
Versionsausgabe und Demo-Rendering. Das ist kein Hardware- oder Cinnamon-Test.

Actions sind auf vollständige Commit-SHAs festgelegt; Dependabot schlägt
Aktualisierungen vor. Die vollständige SHA-Pinnung und geringe Berechtigungen
folgen den [GitHub-Sicherheitsempfehlungen](https://docs.github.com/en/actions/reference/security/secure-use).
Nach der Veröffentlichung muss der tatsächliche Lauf geprüft werden.

Ein `v*`-Tag kann den [Release-Workflow](../.github/workflows/release.yml) starten.
Er akzeptiert nur einen Tag passend zur CMake-Version, testet erneut und erzeugt
einen **Entwurf** mit Quell-ZIP und Prüfsumme. Der Maintainer prüft und
veröffentlicht den Entwurf manuell. Die erstmalige Einrichtung steht unter
[GitHub vorbereiten](GITHUB_SETUP.md).
