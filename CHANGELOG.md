# Änderungsübersicht

## v1.03 – 2026-09-06

- Verbindungseinstellung und Kommandozeilenhilfe nennen IPv4, IPv6 und
  DNS-/mDNS-Hostnamen ausdrücklich; die Übergabe an das Backend ist getestet.
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
