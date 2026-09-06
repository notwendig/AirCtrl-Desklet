# GitHub-Veröffentlichung und Releases

Das öffentliche Repository ist `notwendig/AirCtrl-Desklet`. Jürgen führt als
Maintainer Commits, Tags und Veröffentlichungen nach seiner Prüfung selbst aus.

## 1. Lokal prüfen

Entpacke den ZIP in ein neues Verzeichnis; mische ihn nicht ungeprüft mit einer
bestehenden Git-Arbeitskopie. Im enthaltenen Ordner `AirCtrl-Desklet`:

```bash
python3 scripts/check_repository.py
python3 -m unittest discover -s tests -p 'test_repository.py' -v
cmake --preset ci
cmake --build --preset ci --parallel 2
ctest --preset ci
```

Installationsabhängigkeiten: [DEVELOPMENT.md](DEVELOPMENT.md).
Prüfe README-Bilder, Rollen, Lizenz, Quellcode und Dateien vor der Freigabe.
Insbesondere keine echten Diagnoseberichte, Gerätekennungen, Mitschnitte,
Zugangsdaten oder Buildverzeichnisse veröffentlichen. `.gitignore` allein entfernt
keine bereits versionierten Dateien. Der historische Standardhost im Quellcode
ist aus Kompatibilitätsgründen erhalten; er ist keine automatische Geräteerkennung.

## 2. Neues Repository lokal initialisieren

Nur für den frisch entpackten Ordner **ohne vorhandenes `.git`**:

```bash
git init -b main
git status --short
git add .
git diff --cached --stat
git diff --cached --check
git diff --cached
```

Prüfe die vorgemerkten Dateien. Erst danach:

```bash
git commit -m "Prepare AirCtrl-Desklet v1.03"
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

> Compact Qt 6 desktop controller for Philips AC2729 air purifiers. Local CoAP, live status, Lua automation, diagnostics and maintenance alerts.

Passende Topics: `qt6`, `cpp17`, `linux`, `cinnamon`, `philips`, `air-purifier`,
`coap`, `lua`, `home-automation`, `desktop-widget`, `fedora`.

## 4. GitHub-Einstellungen prüfen

- Ersten CI-Lauf abwarten und Logs lesen; dieses Paket behauptet keinen vorherigen
  grünen GitHub-Lauf. Branch-Schutz erst an tatsächliche Check-Namen binden.
- `main` vor versehentlichem Löschen und Force-Push schützen. Pull Requests und
  erfolgreiche Build-Checks vor dem Zusammenführen empfehlen; Solo-Maintainer
  brauchen keine unerfüllbare zweite Freigabe.
- Unter Security die private Schwachstellenmeldung aktivieren und prüfen, ob
  der in SECURITY.md beschriebene Meldeweg angeboten wird.
- Dependabot aktivieren/prüfen. Vorgeschlagene Action-Updates durch CI absichern.
- Allgemeine Workflowberechtigungen lesend halten; der Release-Job verlangt
  seine Schreibberechtigung ausdrücklich im Workflow.
- Wenn verfügbar, Secret Scanning/Push Protection einschalten. Das ersetzt
  keine manuelle Prüfung von Gerätenummern und Screenshots.

## 5. Release v1.03 als Entwurf

Erst nach erfolgreicher CI und lokalem Gegenprüfen:

```bash
git tag -a v1.03 -m "AirCtrl-Desklet v1.03"
git push origin v1.03
```

Der vorbereitete Release-Workflow baut und testet den Tag erneut. Er erzeugt
einen **nicht veröffentlichten Entwurf** mit Quell-ZIP, SHA-256-Datei und den
[Release-Notizen](releases/v1.03.md). Prüfe Dateien und Grenzen, bevor du im
GitHub-Release auf „Publish release“ klickst. Bei Wiederholung wird ein
vorhandener Release nicht überschrieben.

Für spätere Versionen CMake-Version, READMEs, CHANGELOG und Release-Notizen
gemeinsam aktualisieren. ZIP und Prüfsumme sind ein Quellpaket, kein fertiges
RPM/Flatpak und keine signierte Binärdistribution.
