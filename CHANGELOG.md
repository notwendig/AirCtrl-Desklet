# Änderungsübersicht

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
- Rollen von Jürgen Sievers und OpenAI Codex sowie betaboons Ursprung dokumentiert.
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
