## v2.00 Stable – 2026-09-25

- Dokumentation und Beispiele auf Client-, Server- und Benutzerrollen umgestellt; persönliche Host- und Home-Verzeichnisangaben entfernt.
- Client-Standardziel ist `localhost:5680`; für einen getrennten Server dessen Hostname oder IP-Adresse konfigurieren.
- Versionsnummer in Build und Dokumentation auf 2.00 gesetzt. Ein Einspielskript mit HTTPS-Remote ergänzt. Keine neuen Gerätefunktionen.

# Änderungsübersicht

## v1.09 – 2026-09-14

- Ein mindestens 800 ms langer Druck auf die Timer-Taste sendet ein eigenes
  Ereignis an den Server und führt dort die optionale Lua-Funktion
  `on_long_timer()` aus; das normale Timermenü bleibt dabei geschlossen.
- `on_long_timer()` erhält keine Argumente und darf genau einen
  `airctrl.set { ... }`-Auftrag auslösen. Als ausdrückliche Benutzeraktion
  funktioniert der Langdruck auch bei aktiver manueller Automatik-Sperre.
- Manuelle Änderungen von Power, Betriebsart/Lüfterstufe, Zielfeuchte,
  Gerätefunktion oder Timer sperren die serverseitige Lua-Automatik für alle
  Clients.
- Licht/Anzeige (`aqil`, `uil`) und Kindersicherung (`cl`) lösen die Sperre
  ausdrücklich nicht aus.
- Während der Sperre blinkt die Power-Taste aller verbundenen Clients langsam.
  Ein Klick hebt nur die Sperre auf, schaltet nicht den Gerätestrom und wertet
  Statusregeln sowie den aktuell gültigen Zeitplan sofort neu aus.
- Die manuelle Sperre wird in der serverseitigen Zustandsdatei gespeichert und
  bleibt daher bei einem Serverneustart erhalten.

## v1.08 – 2026-09-14

- Das Desklet prüft seine TCP-Verbindung im Leerlauf per `ping`/`pong`. Ein
  still abgerissener Server oder Netzpfad wird ohne vorherigen Schaltvorgang
  erkannt und automatisch neu verbunden.
- Der Server sendet einem neu verbundenen Client weiterhin sofort seinen
  Zustand, den letzten bestätigten Gerätestatus und den serverseitigen
  Lua-Zustand.
- Das mitgelieferte Tag/Nacht-Skript verwendet nun eine vollständige Regel
  `between = "07:00-22:00"`: `set` gilt ab 07:00 Uhr, `outside` ab 22:00 Uhr.
- Die serverseitige Lua-Dateiübertragung wurde erneut mit zwei Clients geprüft:
  genau eine Editier-Sperre, Speichern zurück auf den Server und sofortige
  Sperrfreigabe auch beim Abbruch der Eigentümerverbindung.
- Frische Server- und Client-Builds mit `-Wall -Wextra -Wpedantic` bleiben bei
  **0 Warnungen**; sämtliche CTest- und Repositorytests bestehen.

## v1.07 – 2026-09-14

- Lua 5.4.9, Ereignisse und Zeitpläne laufen ausschließlich im Qt-freien
  `airctrl-server` und bleiben ohne geöffnetes Desklet aktiv.
- Der Client enthält nur noch den Skripteditor und keine Lua-Laufzeit.
- Beim Öffnen wird das maßgebliche Skript mit Revision vom Server geladen; beim
  Speichern serverseitig validiert, atomar geschrieben und neu geladen.
- Eine serverweite Sperre lässt genau einen Client bearbeiten. Abbrechen oder
  TCP-Verbindungsabbruch gibt sie automatisch frei.
- Aktivierung und behandelte Zeitplantermine werden auf dem Server dauerhaft
  gespeichert. Diagnose und IPC melden den zentralen Lua-Zustand an alle Clients.
- Lua verwendet den portablen `switch`-Dispatcher statt der GCC-Erweiterung für
  berechnete Sprünge; dadurch bleibt der strikte Build bei **0 Warnungen**.

## v1.06 – 2026-09-10

- Jede gültige Gerätestatusmeldung wird mit UTC-Zeitstempel und stabiler
  CSV-Kopfzeile an `/var/log/airctrl.log` angehängt; unbekannte spätere Felder
  bleiben in `_extra_json` erhalten.
- Der Serverinstaller richtet restriktive Dateirechte und Größenrotation ein.
  Das neue headless Matplotlib-Programm zeichnet numerische und boolesche Werte
  mit separaten Achsen und Titeln in eine mehrseitige PDF.
- Stabilitätsnachtrag vom 13. September: Der Geräte-Socket bindet standardmäßig
  UDP 5680 und sendet während stiller Observe-Phasen alle 20 Sekunden ein leeres
  CoAP-CON. So verwerfen zustandsbehaftete Host-Firewalls verzögerte Meldungen
  nicht mehr nach Ablauf eines dynamischen UDP-Eintrags.
- Ein Status-Timeout erneuert Observe zunächst einmal mit demselben Token und
  Socket. Erst ein weiterer Antwort-Timeout ersetzt Socket und Synchronisierung.
- Die erste Observe-Antwort erhält 120 Sekunden; eine endgültige Abmeldung lässt
  dem Gerät 300 ms zum Antworten, bevor der Socket geschlossen wird.
- Wiederholte Aktualisierungsanforderungen bleiben gesperrt, bis der Geräteworker
  den tatsächlich erneuerten I/O-Start gemeldet hat.
- IPC-Eingabeverarbeitung prüft nach jeder potenziell trennenden Ausgabe erneut
  die Clientexistenz und verwendet keinen ungültig gewordenen Container-Iterator.
- Ein frischer Client-Build liest die Lua-Beispielautomatik wieder vor dem
  Erzeugen des eingebetteten Headers ein; die Editorvorlage ist nicht mehr leer
  und hängt nicht von einem zufällig gefüllten CMake-Cache ab.
- `install.sh -s|--server` installiert nur den Qt-freien Server einschließlich
  Konfiguration und Benutzerdienst; `-c|--client` installiert nur Desklet und
  CLI. Ohne Rollenoption bleibt die bisherige Vollinstallation erhalten.
- Der Serverstandard ist wieder `/usr/local/bin`; Client und Desklet bleiben
  unter `$HOME/.local/bin`. Nur der privilegierte Server-Installationsschritt wird
  bei Bedarf über `sudo` ausgeführt, nicht der gesamte Installer.
- Der Installer fasst die ausgewählten CMake-Kompilierungsdatenbanken im
  Projektstamm zusammen. clangd findet dadurch `airctrl_version.hpp` und die
  richtigen C++-/Qt-Includepfade ohne manuelle Editor-Konfiguration.
- Produktionsquellen physisch in `src/server` und `src/client` getrennt. Der
  Server ist vollständig Qt-frei; gemeinsam bleibt nur die Versionsvorlage.
- TCP-Ereignisschleife, INI-Leser und JSON-Behandlung des Servers verwenden
  C++17/POSIX und nlohmann/json statt Qt Core/Network.
- Separate CMake-Konfigurationen erzeugen Debug- und Release-Buildbäume unter
  `build/{DEBUG|RELEASE}/{server|client}`.
- Geräteadresse, UDP-Port und Gerätefristen aus allen Clients entfernt und in
  `/etc/airctrld.cfg` zentralisiert.
- Server-/Client-Verbindung vollständig auf TCP umgestellt; Standardserver im
  Desklet verwendete einen vorkonfigurierten Serverhost auf Port `5680`.
- Das Clientmenü enthält nur Serverhost, TCP-Port und Client-Wiederverbindung.
- Nur `airctrl-server` bindet die Philips-CoAP-Bibliothek ein und kennt
  `AC2729-10:5683`; Desklet, Lua und CLI öffnen keine Geräteverbindung.
- systemd-Dienst startet mit `--config /etc/airctrld.cfg`; vorhandene
  Administratorkonfigurationen werden bei Updates nicht überschrieben.
- TCP-Mehrclienttests und echter UDP-Gerätesimulator verwenden dieselbe neue
  Serverkonfiguration; der v1.05-Mischzustand aus Unix-Socket und TCP ist entfernt.
- Wiederholte Lua-Warnereignisse bei unverändertem Alarm werden verhindert,
  indem die stabile Alarmkennung statt des wechselnden Meldungstextes verglichen wird.
- Eigene C++-Schnittstellen auf Englisch im Doxygen-Stil dokumentiert; optionales
  `Doxyfile` erzeugt die HTML-Referenz außerhalb des Quellbaums.
- CI-Branchfilter vom nicht verwendeten `main` auf den veröffentlichten Branch
  `master` korrigiert.
- Neues geprüftes Einspielskript für Commit und Tag `server_clients` sowie
  atomaren SSH-Push. Ein bereits veröffentlichter Tag `v1.06` bleibt unverändert;
  nur falls er noch fehlt, wird er zusammen mit `server_clients` erzeugt.

## v1.05 – 2026-09-08

- Neue echte Server-/Client-Architektur: Nur `airctrl-server` bindet die
  Philips-CoAP-Bibliothek ein und kommuniziert mit dem AC2729-10.
- Desklet, Lua-Automatik und `airctrl-client` verwenden ausschließlich den
  benutzergeschützten Unix-Socket unter `$XDG_RUNTIME_DIR/airctrl-desklet`.
- Ein Server verteilt denselben bestätigten Statusstrom an mehrere Clients;
  Schaltergebnisse werden nur dem jeweiligen Auftraggeber zugeordnet.
- Gleichzeitige Clientaufträge werden serverweit serialisiert; zwischen zwei
  Geräteversuchen muss mindestens ein neuer Status eingegangen sein.
- Wird Geräte-I/O während eines laufenden Auftrags erneuert, bleibt dessen Ausgang
  ausdrücklich unbekannt; der Auftrag wird nicht wiederholt.
- Das Beenden eines Desklets lässt Server, UDP-Socket und Observe weiterlaufen.
- Nach 90 Sekunden ohne Status erneuert der Server nur seine Geräte-I/O-Sitzung
  (close/open plus `/sys/dev/sync`); die IPC-Clients bleiben verbunden.
- Neuer CLI-Client für `status`, `watch`, `set`, `refresh` und `server-status`.
- `install.sh` installiert und aktiviert einen systemd-Benutzerdienst. Fehlt
  eine Benutzersitzung, startet das Desklet den Server bei Bedarf.
- Lokales IPC-Protokoll, Sicherheitsgrenzen, Diagnose und Testmodell dokumentiert.
- Das frühere direkt zugreifende `airctrl-backend` wird nicht mehr installiert.
- CTest lässt der vollständigen Desklet-/Server-Suite nun 300 statt 90 Sekunden;
  langsamere Rechner werden nicht mehr mitten im Testlauf abgebrochen.
- Das Einspielskript kann einen einzelnen, noch nicht veröffentlichten
  v1.05-Commit sicher aktualisieren und anschließend taggen und pushen.
- Der lokale Verbindungswächter wird vor `connectToServer()` aktiviert. Eine
  sofort erfolgreiche Unix-Socket-Verbindung wird dadurch nicht mehr nach
  drei Sekunden von einem nachträglich gestarteten alten Wächter getrennt.
- Der IPC-Fake-Server pausiert Statusmeldungen jetzt wie der echte Server,
  solange ein Schaltbefehl blockiert ist; Testprozesse und Ereignisfilter
  werden auch nach einer fehlgeschlagenen Assertion sicher bereinigt.
- Das Einspielskript löscht vor CTest ausschließlich das dedizierte Verzeichnis
  `build/v1.05-update`. Damit können reproduzierbare alte ZIP-Zeitstempel keine
  veralteten Objektdateien oder Testprogramme wiederverwenden.
- Doppelte unmittelbar aufeinanderfolgende Aktualisierungsanforderungen werden
  in Client und Server zu genau einem Geräte-I/O-Neustart zusammengefasst.
- Ein beim I/O-Abbruch wartender Schaltauftrag wird verworfen und eindeutig als
  fehlgeschlagen gemeldet; er gelangt niemals in die Ersatzsitzung.
- Abgekoppelte Testserver erben keine CTest-Ausgabepipes mehr und beenden sich
  testweise auch dann, wenn vor der ersten Clientverbindung abgebrochen wird.
- Erwartete Fehlerfälle der Tests erzeugen keine Benachrichtigungen mehr in der
  echten Desktopsitzung. Die private D-Bus-Integrationsprüfung bleibt aktiv.
- Der Fortsetzungsprüfer erkennt auch die bereits lokal vorhandenen v1.05-
  Zwischenstände sowie die versehentliche Betreffzeile mit doppeltem `and` und
  korrigiert sie beim Amend auf den endgültigen Commit-Betreff.

## v1.04 – 2026-09-07

Dokumentationsnachtrag vom **2026-09-08**, Anwendungsversion weiterhin 1.04:

- Zwei Gerätemitschnitte ausgewertet: 148 gültige Statusmeldungen, 17/17
  Schaltbefehle durch den jeweils nächsten Status nach 45–97 ms bestätigt.
- Timeout-Erneuerung dokumentiert: Abmeldung nach 90 s, neuer Port und Sync
  nach weiteren 9,7 s, insgesamt 136,1 s bis zu frischen Daten.
- Fortlaufende Sendezähler ohne Resync beim Schalten belegt. Der Begriff
  Session-Key wird präzisiert: synchronisierte Richtungszähler; AES-Schlüssel
  und IV werden aus dem jeweiligen Nachrichtenpräfix abgeleitet.
- `dtrs` in Lua-Beispiel, Benutzerhandbuch und Diagnose als beobachtete
  verbleibende Timer-Minuten erläutert (`dt=6`, `360 → 359` nach etwa 60 s).
  Keine zusätzliche Schreibmöglichkeit und keine Änderung der Rohwerte.
- READMEs, Architektur, Prüfbericht und Release-Anleitung abgeglichen;
  Rohmitschnitte und Gerätekennungen werden nicht ins Quellpaket aufgenommen.

- Statusbeobachtung und Schaltbefehle verwenden genau einen dauerhaften
  Backend-Prozess, UDP-Socket und synchronisierten Protokollzustand.
- Vor einem Schaltbefehl wird Observe abgemeldet; Control und anschließende
  Observe-Neuanmeldung laufen nacheinander auf demselben Socket.
- Ein ausbleibender erster oder späterer Status beendet die I/O-Sitzung. Bei der
  Wiederverbindung wird der Socket geschlossen/geöffnet und einmal neu synchronisiert.
- Ein Schaltfehler erneuert weder Socket noch Synchronisierung und wird nicht automatisch
  wiederholt; der bestätigte Gerätestatus bleibt maßgeblich.
- Reale UDP-Integrationstests prüfen denselben Quellport und nur einen Sync beim
  Schalten sowie neuen Port und neuen Sync nach einem Status-Timeout.

## v1.03 – 2026-09-06

- Verbindungseinstellung und Kommandozeilenhilfe nennen IPv4, IPv6 und
  DNS-/mDNS-Hostnamen ausdrücklich; die Übergabe an das Backend ist getestet.
- Der voreingestellte Host lautet `AC2729-10`.
- Bereits gespeicherte Varianten `AC2729/10` und `AC2729_10` werden automatisch
  auf den gültigen Hostnamen `AC2729-10` korrigiert; gespeicherte IP-Adressen bleiben erhalten.
- Das Einstellungsfeld heißt sichtbar „IP oder Host“; per Maus öffnet nur die
  rechte Taste das Kontextmenü.
- `install.sh` führt einen sauberen Neubau aus, damit keine ältere Oberfläche
  aus einem vorhandenen Build-Verzeichnis weiterverwendet wird.
- Die konfigurierte Hintergrundfarbe und Transparenz füllen die gesamte
  Desklet-Fläche einschließlich aller Ecken aus.
- IPv6-Adressen erscheinen in Diagnose-Endpunkten eindeutig in eckigen Klammern.
- `examples/automation.lua` dokumentiert als Kommentar alle sieben Ereignisse,
  ihre Felder, alle erlaubten Steuerwerte und die bekannten AC2729-Statusfelder
  samt Bedeutung beziehungsweise ausdrücklich ungesicherter Bedeutung.
- Editorvorlage und installiertes Beispiel stammen nun aus derselben Quelldatei;
  ein Test verhindert ein unbemerktes Auseinanderlaufen.

## v1.02 – 2026-09-06

- Eingebettetes Lua 5.4.9 aus dem per SHA-256 geprüften offiziellen Quellarchiv.
- Neue, standardmäßig ausgeschaltete Lua-Automatik mit integriertem Editor,
  Syntax-/API-Prüfung, Neuladen, Diagnose und sichtbarem Fehleralarm.
- Ereignisse für Start, Minute, Verbindung, bestätigten Status einschließlich
  Feldänderungen, Alarmänderungen und Resultate automatischer Befehle.
- Lokale Zeitpläne mit Wochentagen, Tag/Nacht-Beispiel, optionalem Nachholen des
  jüngsten Termins und dauerhaftem Schutz vor mehrfacher Ausführung.
- `airctrl.set` benutzt dieselbe Positivliste, Ein-Befehl-Sperre und
  Statusbestätigung wie die Oberfläche; bereits passende Werte werden nicht gesendet.
- Sandbox ohne Datei-, Netzwerk-, Shell-, Prozess-, Paket- oder Debugzugriff;
  256-KiB-Skript-, 8-MiB-Speicher- und 200.000-Instruktionsgrenze.
- Separate Automatiktests sowie ergänzte Architektur-, Sicherheits-, API-,
  Lizenz- und GitHub-Dokumentation.

## GitHub-Projektvorbereitung – 2026-09-03

Anwendungsstand bleibt **v1.01**; kein neues Protokoll- oder Geräteverhalten.

- Deutsche und englische README mit echtem Nutzerbild und neutralen Qt-Demobildern.
- Rollen des Projektinitiators und von OpenAI Codex sowie betaboons Ursprung dokumentiert.
- MIT-/Herkunftshinweise, Beitragsregeln, Sicherheitshinweise und Issue-/PR-Vorlagen.
- CMake-Presets; gemeinsame generierte Versionskennung für Anwendung und Tests.
- Vorbereitete Ubuntu-/Fedora-CI und taggebundener Release-Entwurf, SHA-gepinnte Actions.
- Offline-Repositoryprüfung, reproduzierbarer Quell-ZIP, Prüfsumme und Pakettests.
- GitHub-Einrichtungsanleitung; noch kein Repository angelegt oder veröffentlicht.

## v1.01 – 2026-09-03

- AC2729: A3/HEPA, C7/Aktivkohle und F1/Befeuchtungsdocht jeweils einzeln mit
  lokaler Vorwarnung bei 1–120 Restbetriebsstunden; bei 0 h roter Alarm.
- F1 ist ein Austauschhinweis. `wicksts=0` löst keinen Reinigungsalarm mehr aus.
- Keine Behauptung einer bekannten Philips-120-h-Schwelle oder Decodierung von
  `0xC054`; Diagnose und README nennen ausdrücklich die lokale Warnlogik.
- Individuelle Alarmidentitäten: keine Wiederholung beim stündlichen Zählerwechsel,
  aber erneute Meldung beim Ablauf oder nach Behebung und Wiederauftreten.
- Diagnosekopie: Clipboard plus unterstützte PRIMARY-Auswahl, sichtbare Rückmeldung,
  Strg+Umschalt+C, vollständiger markierbarer Kopierbericht; Öffnen nach Menüabbau.
- Datenalter-/Alarmkreise bei Standardschrift 26 statt 40 px. Die Sekunden stehen
  einzeilig im Kreis; Einheit und Details im Tooltip. Schriftgröße bleibt skalierbar.
- Vollständiges Quellpaket. Empfang, Schaltbefehle, Installer und Einstellungen
  bleiben erhalten. Native Cinnamon-/Wayland-Zwischenablage hier nicht verifiziert.

## v1.00 Stable – 2026-09-02

Erste stabile Ausgabe, unveränderter Funktionsstand von 0.3.8.

- Versionskennung in CMake, Programm, Diagnose und Dokumentation auf 1.00 gesetzt.
- Vollständiges Quellpaket mit Installer, Backend, Tests und Vorschauen.
- Bestehende Einstellungen, Autostart und Installationspfade bleiben erhalten.
- Keine Änderung an CoAP-Kommunikation, Schaltbefehlen, Oberfläche oder Alarmverhalten.

Enthalten sind die acht Gerätetasten, aktive Status-Embleme, runde Datenalter- und
Alarmanzeigen, anpassbare Darstellung, der gespeicherte Haken für die
Fensterdekoration sowie die Diagnose mit Hexcodes und unveränderten Rohdaten.

Die Bezeichnung Stable ist keine zusätzliche Hardware- oder Desktop-Zertifizierung.
Der Umfang der automatisierten Prüfungen und die weiterhin nicht in einer echten
Cinnamon-Sitzung geprüften Funktionen stehen in VALIDATION.md.

## 0.3.8

- Fensterdekoration über einen gespeicherten Kontextmenü-Haken umschaltbar.
- Fehler-/Statuscodes zusätzlich als Hexwerte in Diagnose und Kopierbericht.
- Datenalter und Alarm als Kreise neben den Emblemen im Statusfeld.

## 0.3.7

- Sekundenzähler seit dem letzten gültigen Statuspaket mit konfigurierbarer Ampel.
- Sichtbare Warnungen/Fehler, Alarmdetails und Quittierung.
- Desktop-Benachrichtigungen und optionaler Signalton.

## 0.3.6

- Aktive Display-Embleme aus dem bestätigten Gerätestatus.

## 0.3.5

- Dauerhafte CoAP-Beobachtung statt zyklischer Einzelabfragen.
- Getrennte Schreibprozesse, bestätigte Schaltzustände und Elternprozessschutz.
