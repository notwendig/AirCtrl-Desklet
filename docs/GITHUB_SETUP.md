# GitHub-Veröffentlichung und Releases

Das öffentliche Repository ist [notwendig/AirCtrl-Desklet](https://github.com/notwendig/AirCtrl-Desklet). Der Maintainer prüft Commits, Tags und Releases vor der Veröffentlichung.

## Aktuellen Stand prüfen

```bash
git clone https://github.com/notwendig/AirCtrl-Desklet.git
cd AirCtrl-Desklet
python3 scripts/check_repository.py
python3 -m unittest discover -s tests -p 'test_repository.py' -v
cmake --preset release-server
cmake --build --preset release-server --parallel 2
ctest --preset release-server
cmake --preset release-client
cmake --build --preset release-client --parallel 2
ctest --preset release-client
```

Die Abhängigkeiten stehen in [DEVELOPMENT.md](DEVELOPMENT.md). Im getrennten Betrieb trägt der Client den tatsächlichen Serverhost ein. Das voreingestellte `localhost:5680` gilt für Client und Server auf demselben Rechner. Geräteeinstellungen liegen ausschließlich in `/etc/airctrld.cfg` auf dem Server. Vor der Freigabe Änderungen, Bilder, Diagnoseberichte, Gerätekennungen und den Git-Diff prüfen.

## v2.00 aus dem Paket einspielen

Nach dem Entpacken im Projektordner:

```bash
bash einspielen-v2.00.sh
```

Das Skript prüft Paket, Builds und Tests, übernimmt die Dateien in einen temporären Git-Worktree und pusht erst danach Commit und gegebenenfalls Tag `v2.00` atomar über HTTPS. Es verlangt einen sauberen lokalen `master`; einen älteren Stand bewahrt es als Backup-Branch. `AIRCTRL_PROJECT_DIR` kann den Zielordner setzen. Mit `AIRCTRL_SKIP_PUSH=1` bleibt alles lokal. Der Push erfolgt nur, wenn der Maintainer das Skript selbst ausführt.

## GitHub-Einstellungen prüfen

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

## Release-Entwurf prüfen

Der Release-Workflow baut und testet den Tag erneut. Er erstellt einen unveröffentlichten Entwurf mit Quell-ZIP, Prüfsumme und den [Release-Notizen](releases/v2.00.md). Der Maintainer prüft den Entwurf vor der Veröffentlichung.

Für spätere Versionen CMake-Version, READMEs, CHANGELOG und Release-Notizen
gemeinsam aktualisieren. ZIP und Prüfsumme sind ein Quellpaket, kein fertiges
RPM/Flatpak und keine signierte Binärdistribution.
