# Validierung – v1.00 Stable

Datum: 2026-09-02.

Stable-Freigabe des Funktionsstands 0.3.8 auf Benutzerwunsch. Nur die
Release-Kennung und Dokumentation wurden geändert; es gibt keine neue Geräte-,
Fenster- oder Alarmlogik. Die unten genannten Testgrenzen bleiben bestehen.
Der Dateivergleich mit dem 0.3.8-ZIP bestätigt unveränderte Laufzeitquellen und
Tests; in main.cpp und CMakeLists.txt unterscheidet sich nur die Versionsangabe.
Die installierte Demo-Vorschau ist ebenfalls bytegleich mit 0.3.8.

## Build und Installation

- Linux x86-64, Ubuntu 24.04; GCC 13.3.0, C++17, Release.
- Qt 6.8.3 (Core, Gui, Widgets, DBus, Test), OpenSSL 3.0.13, nlohmann/json 3.12.0.
- Oberfläche, Backend und Testprogramme erfolgreich kompiliert.
- Keine Compilerwarnungen aus dem UI-Code mit -Wall -Wextra -Wpedantic.
- install.sh in einen separaten absoluten Testpräfix ausgeführt.
- Installierte GUI meldet „airctrl-desklet 1.00“.
- Test-SDK, Buildverzeichnisse und Binärdateien sind nicht im Projekt-ZIP enthalten.
  Unter Fedora werden weiterhin die bereits genannten Systempakete verwendet.

## Automatisierte Prüfungen

QtTest/CTest mit Qt-Offscreen:

    Totals: 85 passed, 0 failed, 2 skipped, 0 blacklisted
    100% tests passed out of 1

Die Zählung enthält Initialisierung/Aufräumen und parametrisierte Testfälle.
Der native X11-Test wird ohne X11-Sitzung übersprungen. Die direkte Zustellung
an einen D-Bus-Testdienst wird ebenfalls übersprungen: Bereits bei 0.3.7 verweigerte
die Testumgebung das Anlegen des lokalen D-Bus-Sockets (Operation not permitted).
Es wurde kein alternativer Zugriff auf eine echte Benutzersitzung versucht.

| Bereich | Prüfung |
|---|---|
| Fensterdekoration | Kontextmenü-Haken in beide Richtungen; sichtbares Fenster, unveränderte Größe, gespeicherte Auswahl, nur ein weiterlaufender Empfänger |
| Dekorations-Migration | Bestehendes window/desktop wird bei fehlendem neuen Schlüssel übernommen; danach separat gespeicherte Auswahl |
| X11-/Wayland-Routing | Umschalten unter simuliertem Sitzungstyp, X11-Position erhalten, gespeicherte Wayland-Koordinaten nicht überschrieben; keine Prüfung echter Compositor-Dekoration |
| Runde Anzeigen | Zwei echte Kreise im Statusfeld; transparente Ecken, unverzerrte Geometrie, Farbwechsel und Alarmanzahl; Schriftgrößen 6/10/24/48 |
| Langer Datenstillstand | 123456 Sekunden weiterhin exakt als Sekunden; keine Änderung der Fenstergröße |
| Diagnose Hex | 15 Datensätze für sieben Code-Tags; u.a. 49236 = 0xC054, Dezimalstrings, Null, große Zahlen, Überlauf und ungültige Typen |
| Rohdatenschutz | Hexdarstellung in eigener Spalte und Kopierbericht, unveränderte empfangene Werte/JSON; keine Umdeutung von Messwerten oder Filterstunden |
| Datenalter | Kein Empfang = — s; grün bei 0/44 s, gelb bei 45/89 s, rot bei 90 s; genaue Grenzübergänge |
| Zeitbasis | Injizierbare monotone Testuhr; Produktionsuhr unter Linux CLOCK_BOOTTIME; realer Sekundentakt im 19-s-Pausentest |
| Empfang während Schreiben | Jedes gültige Paket setzt den Zähler zurück, ohne den unbestätigten Schaltzustand zu übernehmen |
| Ungültige Daten / ACKs | Ungültiges JSON und reine Schreibannahme setzen das Datenalter nicht zurück |
| Alarm-Latch | Einmalige Warnung, Verschärfung zu Fehler, Quittierung, keine Timeout-Flut, Wiederauftreten nach Behebung |
| Gerätewarnungen | Filter-/Wasserwarnungen gemeinsam, anhaltend und wiederkehrend; 49236 allein löst keinen Alarm aus |
| Schaltfehler | Aktiver Fehler bei weiterhin grünem Datenalter; Quittierung entfernt vergangene Meldung |
| Alarmeinstellungen | Speichern/Abbrechen, Grenzprüfung, Roundtrip, individuelle Schwellen, keine Geräteschreibzugriffe |
| Alarmfenster | Linksklick öffnet Details, Quittierung sichtbar; Rechtsklick und Ziehen funktionieren |
| D-Bus-Nachricht | Dienst/Pfad/Methode, Argumenttypen, Dringlichkeit als Byte, HTML-Escaping und Zeitlimit |
| Demo | Alarmdarstellung mit Testdaten, keine Benachrichtigungen oder Töne |
| Emblem-Modi | Automatik, Ruhe, Allergen, manuelle Stufen 1/2/3 und Turbo; mode=P mit om=s bleibt Automatik |
| Emblem-Funktionen | Luftreinigung / 2-in-1, Kindersicherung, Timer, PM2.5 / IAI und unbekannte Anzeigecodes |
| Alarmgrenzen | AC2729-Modellprüfung, bekannte Codes, abgelaufene Zähler; 49236 und Typkennungen A3/C7 erzeugen keinen Alarm |
| Ungültige Daten | Fehlende/null/bools/falsche Strings/negative/gebrochene Zähler lösen keine Warnsymbole aus |
| Bestätigte Embleme | Power-Klick lässt die Anzeige bis zur tatsächlichen Rückmeldung unverändert; nur ein Beobachtungsprozess |
| Offline-Embleme | Letzte Symbole abgeblendet, WLAN orange/durchgestrichen, Tooltips markieren veralteten Status |
| Emblem-Layout | 287 × 142 Pixel mit zwei Statuskreisen, bis zu neun Symbolen in zwei Reihen; Schriftgrößen 10/24/48, Transparenz und Vordergrundfarbe |
| Emblem-Bedienung | Linksklick/Rechtsklick öffnet genau ein Menü; Ziehen verschiebt ohne Menü oder Geräteschreibzugriff |
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

Die seit 0.3.5 vorhandene Prüfung über die direkte Exit-Bestätigung des
Testempfängers wurde unverändert erneut erfolgreich ausgeführt.

### Transparenzprüfung der Emblemzeile

Die erste Testvariante prüfte die Emblemzeile über einen isolierten Child-Grab.
Qt ergänzte dabei einen deckenden Palettenhintergrund. Der Test prüft jetzt die
tatsächlich zusammengesetzte Oberfläche über den Top-Level-Grab: Der freie
Hintergrund der Emblemzeile ist bei 100 % Transparenz ebenfalls transparent.
Die produktive Darstellung musste dafür nicht geändert werden.

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

Der nachfolgende Benutzermitschnitt von 0.3.5 bestätigte anschließend den Betrieb
am echten Gerät: 385,35 Sekunden zwischen Anmeldung und Abmeldung, 39 gültige
Statusmeldungen mit fortlaufender Sequenz, 13 Schaltbefehle mit success-Antwort
und jeweils passender nächster Statusmeldung. Die längste Pause während dieser
Sitzung betrug 37,07 Sekunden; es gab keine Neuanmeldung. ICMP Port unreachable
trat nur vor der Sitzung und nach ihrer Abmeldung auf.

## Oberfläche, Diagnose und Kompatibilität

Die acht Buttons und ihre Power-Farben bleiben unverändert. Unter den Buttons
steht das Statusfeld mit Emblemen und zwei Kreisen für Datenalter/Alarme; darunter
folgen die Messwerte. Standardgröße ohne native Fensterdekoration:
287 × 142 Pixel, bei größerer Schrift mitwachsend. Das aktive Set folgt nur den
bestätigten Statuswerten; es verursacht keine Geräteabfragen oder Schaltbefehle.

Die installierte echte Qt-Oberfläche wurde als vorschau.png gerendert. Weitere
Qt-Renderings: power-vorschau.png mit drei Power-Zuständen, embleme-vorschau.png
mit mehreren Modi und ausdrücklich simulierten Wartungswarnungen,
alarme-vorschau.png mit drei Datenalterfarben und Gerätewarnung sowie
diagnose-vorschau.png. Die Vorschauen wurden visuell geprüft.

Die Diagnose erklärt jetzt Empfangsmodus, Empfangsphase, Zahl der Statusmeldungen,
Beobachtungsstarts, Anlauf-/Stillstandsfristen, Bestätigung und Wiederverbindung.
Ein Schaltfehler bleibt separat sichtbar, ohne eine gesunde Beobachtung auf
offline zu setzen.

Der alte gespeicherte Intervallwert bleibt erhalten und wird als Wiederverbindungspause
nach einem Fehler genutzt. Darstellung und übrige Einstellungen bleiben erhalten.

Der CoAP-Protokollcode und der Controller bleiben gegenüber 0.3.7 unverändert.
Das dort ergänzte Signal für jedes vollständig gelesene gültige Statuspaket
verhindert, dass ein Schreibauftrag das Datenalter verfälscht. Timeouts,
Wiederverbindung und Schreibbefehle wurden nicht verändert. Die vollständige frühere separate
Python/UDP-Protokolltestsuite wurde nicht erneut ausgeführt; der oben beschriebene
reale UDP-Integrationstest ist Bestandteil der erneut erfolgreichen Qt-Testsuite.

## Grenzen der Prüfung

Der D-Bus-Integrationstest ist enthalten und kann über AIRCTRL_TEST_WITH_DBUS=ON
in einer eigenen dbus-run-session laufen. Hier scheiterte das Starten dieser
Testsitzung bei 0.3.7 an der Socket-Berechtigung. Die tatsächliche Cinnamon-Zustellung,
deren Verhalten bei „Nicht stören“ und die Hörbarkeit des optionalen Systemtons
sind daher nicht bestätigt. Ohne Benachrichtigungsdienst bleiben die sichtbaren
Alarme und ihre Details im Widget erhalten; es gibt keinen automatischen
Geräteeingriff als Reaktion auf einen Alarm.

Die bei 0.3.7 bereitgestellten CMake-/JSON-Hilfsabhängigkeiten wurden wiederverwendet
und sind nicht Bestandteil des ZIPs. Für v1.00 wurde in einem neuen Buildverzeichnis
konfiguriert und gebaut; die vollständige Testsuite wurde erneut ausgeführt.
Build, Installation und Tests erfolgten lokal, nicht auf dem Benutzerrechner.

Keine laufende Cinnamon/Muffin- oder echte Wayland-Sitzung in dieser Umgebung.
Native Titelleiste/Rahmen, Popup-Darstellung, Desktop-Ebene, Tray, interaktives Verschieben und Autostart müssen
weiterhin in der Benutzersitzung geprüft werden.

Kein eigener Zugriff auf den physischen AC2729/10. Der Benutzer hat den Empfang
und die Steuerung mit 0.3.5 am Gerät bestätigt. Die neue Emblemzeile ist mit
synthetischen Zuständen geprüft; reale Filter- oder Wasseralarme wurden nicht
ausgelöst oder nachgestellt. Wartungssymbole sind Hinweise aus bekannten Codes
und abgelaufenen Zählern, keine vollständige Garantie einer identischen
Firmware-Displayanzeige. Unbekannte Codes bleiben in der Diagnose sichtbar.
AT-SPI-Startmeldungen werden durch dieses Update nicht verändert.
