# GitHub-Veröffentlichung und Releases

Das öffentliche Repository ist `notwendig/AirCtrl-Desklet`. Jürgen führt als
Maintainer Commits, Tags und Veröffentlichungen nach seiner Prüfung selbst aus.

## Direkte Aktualisierung des vorhandenen Originals

Das v1.06-Übergabe-ZIP enthält `AirCtrl-Desklet/einspielen-v1.06.sh`. Das Skript
arbeitet standardmäßig ausschließlich auf `~/Projects/Qt/AirCtrl-Desklet`. Es
verlangt einen sauberen Git-Arbeitsbaum, kopiert anhand der öffentlichen
Positivliste, prüft Repository, Build und Tests, erstellt den Commit und den
annotierten Tag `server_clients` und pusht Branch plus Tag atomar. Ein bereits
veröffentlichter Tag `v1.06` bleibt unverändert; nur wenn er noch fehlt, wird er
am selben Commit zusätzlich erzeugt. Veröffentlicht wird aus einem temporären Worktree direkt auf Grundlage
von `origin/master`; dadurch ist der Push auch bei einem älteren, sauberen lokalen
Commit ein Fast-Forward. Der vorherige lokale Stand bleibt unter
`backup-before-server-clients-<Kurz-ID>` erhalten. Nach erfolgreichem Push wird
der lokale `master` auf den veröffentlichten Commit gesetzt und v1.06 installiert.

```bash
cd ~/Downloads
unzip -o AirCtrl-Desklet-1.06.zip
bash AirCtrl-Desklet/einspielen-v1.06.sh
```

Das Remote wird ausdrücklich auf SSH gesetzt:
`git@github.com:notwendig/AirCtrl-Desklet.git`. Das Skript verwendet weder HTTPS
noch Force-Push. Bei ungesicherten Änderungen, vorhandenem abweichendem Tag,
fehlender Git-Identität, fehlgeschlagenen Tests oder Pushfehler bricht es ab.
Mitschnitte werden weder kopiert noch eingecheckt. Für ein bewusst abweichendes
Projektverzeichnis kann `AIRCTRL_PROJECT_DIR` gesetzt werden.

## 1. Lokal prüfen

Entpacke den ZIP in ein neues Verzeichnis; mische ihn nicht ungeprüft mit einer
bestehenden Git-Arbeitskopie. Im enthaltenen Ordner `AirCtrl-Desklet`:

```bash
python3 scripts/check_repository.py
python3 -m unittest discover -s tests -p 'test_repository.py' -v
cmake --preset release-server
cmake --build --preset release-server --parallel 2
cmake --preset release-client
cmake --build --preset release-client --parallel 2
ctest --preset release-client
```

Installationsabhängigkeiten: [DEVELOPMENT.md](DEVELOPMENT.md).
Prüfe README-Bilder, Rollen, Lizenz, Quellcode und Dateien vor der Freigabe.
Insbesondere keine echten Diagnoseberichte, Gerätekennungen, Mitschnitte,
Zugangsdaten oder Buildverzeichnisse veröffentlichen. `.gitignore` allein entfernt
keine bereits versionierten Dateien. Der Gerätehost `AC2729-10` steht nur in
`/etc/airctrld.cfg`; Clients verwenden standardmäßig den Server `nadhh:5680`.

## 2. Neues Repository lokal initialisieren

Nur für den frisch entpackten Ordner **ohne vorhandenes `.git`**:

```bash
git init -b master
git status --short
git add .
git diff --cached --stat
git diff --cached --check
git diff --cached
```

Prüfe die vorgemerkten Dateien. Erst danach:

```bash
git commit -m "v1.06: TCP server and system device configuration"
```

Git verwendet deine vorhandene Identität. Falls sie fehlt, entscheide selbst
über Name und E-Mail, gegebenenfalls die von GitHub angebotene private
`noreply`-Adresse. Es wurden keine Identitäten konfiguriert und keine Commits
in deinem Namen erstellt. Codex wird in AUTHORS und README als KI-Partner
genannt, nicht mit einer erfundenen Commit-E-Mail.

## 3. Remote prüfen

Das vorhandene SSH-Remote lautet:

```bash
git@github.com:notwendig/AirCtrl-Desklet.git
```

Keinen Force-Push verwenden. Vor dem Push Arbeitsbaum und Remote prüfen.

Empfohlene Beschreibung:

> Qt 6 desktop controller with one Philips AC2729 server, multiple TCP clients, live status, Lua automation and diagnostics.

Passende Topics: `qt6`, `cpp17`, `linux`, `cinnamon`, `philips`, `air-purifier`,
`coap`, `lua`, `home-automation`, `desktop-widget`, `fedora`.

## 4. GitHub-Einstellungen prüfen

- Ersten CI-Lauf abwarten und Logs lesen; dieses Paket behauptet keinen vorherigen
  grünen GitHub-Lauf. Branch-Schutz erst an tatsächliche Check-Namen binden.
- Den veröffentlichten Standardbranch (derzeit `master`) vor versehentlichem
  Löschen und Force-Push schützen. Pull Requests und
  erfolgreiche Build-Checks vor dem Zusammenführen empfehlen; Solo-Maintainer
  brauchen keine unerfüllbare zweite Freigabe.
- Unter Security die private Schwachstellenmeldung aktivieren und prüfen, ob
  der in SECURITY.md beschriebene Meldeweg angeboten wird.
- Dependabot aktivieren/prüfen. Vorgeschlagene Action-Updates durch CI absichern.
- Allgemeine Workflowberechtigungen lesend halten; der Release-Job verlangt
  seine Schreibberechtigung ausdrücklich im Workflow.
- Wenn verfügbar, Secret Scanning/Push Protection einschalten. Das ersetzt
  keine manuelle Prüfung von Gerätenummern und Screenshots.

## 5. Release v1.06 als Entwurf

Das Einspielskript erstellt und pusht `server_clients`. Einen bereits
veröffentlichten Tag `v1.06` lässt es unverändert. Die folgenden Befehle sind
nur die manuelle Alternative, wenn das Skript bewusst nicht verwendet wurde:

```bash
git tag -a server_clients -m "AirCtrl server/client architecture"
git push --atomic origin master server_clients
```

Der vorbereitete Release-Workflow baut und testet den Tag erneut. Er erzeugt
einen **nicht veröffentlichten Entwurf** mit Quell-ZIP, SHA-256-Datei und den
[Release-Notizen](releases/v1.06.md). Prüfe Dateien und Grenzen, bevor du im
GitHub-Release auf „Publish release“ klickst. Bei Wiederholung wird ein
vorhandener Release nicht überschrieben.

Für spätere Versionen CMake-Version, READMEs, CHANGELOG und Release-Notizen
gemeinsam aktualisieren. ZIP und Prüfsumme sind ein Quellpaket, kein fertiges
RPM/Flatpak und keine signierte Binärdistribution.
