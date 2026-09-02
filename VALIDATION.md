# Validierung – Version 0.3.5

Datum: 2026-09-02.

## Build und Installation

- Linux x86-64, Ubuntu 24.04; GCC 13.3.0, C++17, Release.
- Qt 6.8.3 (Core, Gui, Widgets, Test), OpenSSL 3.0.13, nlohmann/json 3.12.0.
- Oberfläche, Backend und Testprogramme erfolgreich kompiliert.
- Keine Compilerwarnungen aus dem UI-Code mit -Wall -Wextra -Wpedantic.
- install.sh in einen separaten absoluten Testpräfix ausgeführt.
- Installierte GUI meldet „airctrl-desklet 0.3.5“.
- Test-SDK, Buildverzeichnisse und Binärdateien sind nicht im Projekt-ZIP enthalten.
  Unter Fedora werden weiterhin die bereits genannten Systempakete verwendet.

## Automatisierte Prüfungen

QtTest/CTest mit Qt-Offscreen:

    Totals: 42 passed, 0 failed, 1 skipped, 0 blacklisted
    100% tests passed out of 1

Die Zählung enthält Initialisierung/Aufräumen und parametrisierte Testfälle.
Der native X11-Test wird ohne X11-Sitzung übersprungen.

| Bereich | Prüfung |
|---|---|
| Dauerbeobachtung | Mehrere Meldungen aus genau einem Prozess; keine periodischen Einzelabfragen |
| Reale Zeitspanne | 19 Sekunden zwischen Meldungen; nach 11 Sekunden weiterhin online; kein Neustart |
| Startparameter | status-observe -J --timeout 60 --idle-timeout 90 |
| Streamingparser | Aufgeteilte JSON-Zeile und mehrere Zeilen in einem Ausgabeblock |
| Eingabeschutz | Ungültiges JSON und übergroße unvollständige Statuszeile führen zum kontrollierten Fehler |
| Wiederverbindung | Unerwartetes Prozessende, ausbleibende erste Meldung, späterer Datenstillstand |
| Lebenszyklus | Stop/Start während Prozessanlauf; F5 startet nur einen Ersatzempfänger; Stop beendet Wiederverbindung |
| Bedienbarkeit | Empfang sperrt die Tasten nicht; ein Schreibauftrag sperrt nur die übrigen Tasten |
| Power | Immer aktiv, Orange/Weiß/Grün, Kindersicherung, fehlender Status, transparente Hintergründe |
| Offline-Power | Ein Einschaltversuch ohne Abwarten der ersten Statusantwort, ohne Empfängerabbruch |
| Schreibbestätigung | Vor Schreibannahme empfangene Meldungen bestätigen den Auftrag nicht |
| Schreibfehler | Kein falscher Offline-Wechsel bei weiterhin gültiger Beobachtung |
| Schreib-Watchdog | Genau ein Versuch; keine automatische Wiederholung |
| Bestätigungsfrist | Ausbleibende Statusbestätigung gibt Bedienung wieder frei; keine Wiederholung |
| Gleichzeitige Wiederverbindung | Laufender Schreibauftrag wird bei Beobachtungsneustart nicht erneut gesendet |
| Echter CoAP-Transport | GUI-Controller und echtes CLI gegen lokalen UDP-Gerätesimulator |
| Elternprozessschutz | Hart beendetes Hilfs-Widget führt nachweislich zu SIGTERM und Ende seines Empfängers |
| Bestehende Funktionen | Panelbefehle/Datentypen, Menüs, Schriftgrößen, Transparenz, Werte, Position, Einstellungen und Autostart |
| Diagnose | Erklärungen, Rohdaten, unbekannte Tags, Kopierbericht |
| Vorschau | Keine Geräteschreibzugriffe |
| Wayland-Routing | Simulierte Sitzung: Fensterflags, Verschiebeanfrage, Menü, keine X/Y-Aktion |

### Echter UDP-Integrationstest

Der Test bindet ausschließlich an 127.0.0.1 mit dynamischem UDP-Port. Er startet
das tatsächlich gebaute airctrl-backend über den GUI-Controller.

Der Simulator beantwortet Synchronisierung und Beobachtungsanmeldung, sendet
verschlüsselte CoAP-Statusmeldungen mit wiederverwendeter Message-ID und
verarbeitet einen Power-Schreibbefehl auf einem separaten Socket. Geprüft werden:

- genau eine Beobachtungsanmeldung;
- eine Synchronisierung für den Empfänger und eine für den Schreibprozess;
- genau ein Schreibauftrag;
- unverändert bestehende Beobachtung während des Schreibens;
- neue Statusmeldung bestätigt pwr="0";
- keine Abmeldung bis zum ausdrücklichen Stopp; danach genau eine Abmeldung.

Dieser Test prüft die Integration mit dem echten Transport, nicht jede Eigenheit
der Philips-Firmware. Die Verschlüsselungsroutine ist in diesem Simulator dieselbe
C++-Routine wie im Client; er ist kein unabhängiger Kryptografie-Test.

### Beenden und Elternprozessschutz

Ein separates Qt-Hilfsprogramm startet einen dauerhaften Empfänger. Der Test
beendet das Hilfsprogramm hart, sodass dessen Destruktor nicht laufen kann.
Der Empfänger dokumentiert anschließend seinen tatsächlichen SIGTERM-Ausstieg.
Dies prüft Linux PR_SET_PDEATHSIG, ohne sich auf Prozessnummern im möglicherweise
anders eingebundenen /proc der Testumgebung zu verlassen.

Die erste Variante dieser Testprüfung über /proc/<pid>/stat schlug bereits bei
der Prüfung des laufenden Kindes fehl. Sie wurde durch die direkte Exit-Bestätigung
des Testempfängers ersetzt. Das Produktionsverhalten wurde dabei nicht abgeschwächt.

## Erkenntnisse aus dem Benutzer-Mitschnitt

Vor dem Umbau wurden PCAPNG und Terminalausgabe rein lesend untersucht.
Der Mitschnitt enthält die erfolgreiche Observe-Sitzung, nicht die
fehlgeschlagenen Widget-Einzelabfragen.

- Synchronisierung: 3,51 ms.
- Erste Statusmeldung: 8,46 s nach Anmeldung.
- Sieben Statusmeldungen während rund 78,5 s bis zum Abbruch.
- Meldungspausen unter anderem 18,05 s, 18,04 s und 19,04 s.
- Alle sieben entschlüsselten Statusobjekte stimmen vollständig mit der
  Terminalausgabe überein; Token und Beobachtungssequenz passen.
- Nach dem Abbruch noch Meldungen an geschlossene Ports, gefolgt von ICMP
  „Port unreachable“.

Die Mitschnittdaten selbst, Gerätekennungen und sonstiger mitgeschnittener
Netzverkehr sind nicht Bestandteil dieses ZIPs.

## Oberfläche, Diagnose und Kompatibilität

Das kompakte Panel und die Power-Farben bleiben unverändert. Die echte Qt-Oberfläche
wurde als vorschau.png und mit drei Power-Zuständen als power-vorschau.png gerendert.
Die Diagnoseansicht steht in diagnose-vorschau.png.

Die Diagnose erklärt jetzt Empfangsmodus, Empfangsphase, Zahl der Statusmeldungen,
Beobachtungsstarts, Anlauf-/Stillstandsfristen, Bestätigung und Wiederverbindung.
Ein Schaltfehler bleibt separat sichtbar, ohne eine gesunde Beobachtung auf
offline zu setzen.

Der alte gespeicherte Intervallwert bleibt erhalten und wird als Wiederverbindungspause
nach einem Fehler genutzt. Darstellung und übrige Einstellungen bleiben erhalten.

Der CoAP-Protokollcode ist gegenüber 0.3.4 unverändert; der Controller nutzt den
bereits vorhandenen und vom Benutzer erfolgreich getesteten Observe-Modus.
Die vollständige frühere Python/UDP-Protokolltestsuite wurde für 0.3.5 nicht erneut
ausgeführt; stattdessen wurde der oben beschriebene reale UDP-Integrationstest
für die veränderte GUI-Anbindung hinzugefügt.

## Grenzen der Prüfung

Keine laufende Cinnamon/Muffin- oder echte Wayland-Sitzung in dieser Umgebung.
Popup-Darstellung, Desktop-Ebene, Tray, interaktives Verschieben und Autostart müssen
weiterhin in der Benutzersitzung geprüft werden.

Kein Zugriff auf den physischen AC2729/10. Der Benutzer hat den CLI-Observe-Modus
am Gerät bestätigt; die neue GUI-Anbindung einschließlich gleichzeitiger
Schreibbefehle wurde hier mit simuliertem Gerät getestet. Das Update ist somit
noch kein Nachweis, dass jede mögliche Ursache eines Netzwerkausfalls behoben ist.
AT-SPI-Startmeldungen werden durch dieses Update nicht verändert.
