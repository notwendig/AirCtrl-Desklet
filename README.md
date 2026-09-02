# Philips AirControl – Qt6-Gerätepanel 0.3.3

Kompaktes C++/Qt6-Desktopwidget für den Philips AC2729/10 „Wohnzimmer“ unter
Cinnamon. Geräteadresse voreingestellt: **192.168.77.5**, UDP-Port **5683**.
Der bereits am Gerät funktionierende C++-CoAP-Code ist vollständig enthalten.

Version 0.3.2 behebt das regelmäßige Ausgrauen der Tasten während der
Statusabfragen. Ein Klick während einer Abfrage wird vorgemerkt und nach deren
erfolgreichem Abschluss einmal ausgeführt. Bis zur anschließenden Geräterückmeldung
sind weitere Steuerbefehle gesperrt.

## Kompakte Oberfläche

Im Widget stehen nur die acht Kontrolltasten und darunter die Werte.
Standardmäßig sind Feuchte, Zielfeuchte, Temperatur und PM2,5 sichtbar.
Das Standardlayout misst **287 × 85 Pixel** bei 100 % Desktopskalierung.
Bei größerer Schrift wächst das Fenster mit, damit nichts abgeschnitten wird.

`vorschau.png` zeigt die echte Qt-Oberfläche. Im Vorschaumodus sind die
Gerätetasten deaktiviert; „Vorschau“ steht im Tooltip und im Kontextmenü.

## Kontextmenü und Darstellung

**Kurzer Linksklick auf einen Messwert oder Rechtsklick auf das Widget** öffnet
das Kontextmenü. Seit Version 0.3.1 werden die Maustasten direkt verarbeitet, auch über
Wertefeldern und Gerätetasten. Ziehen und kurzer Klick werden unterschieden.
Im Tray-Menü gibt es zusätzlich „Menü / Darstellung …“.

**Rechtsklick → Darstellung** bietet:

| Einstellung | Wirkung |
|---|---|
| Hintergrundfarbe | Farbe hinter Tasten und Werten |
| Hintergrundtransparenz | 0 % deckend bis 100 % durchsichtig |
| Vordergrundfarbe | Farbe der Zahlen, Beschriftungen und Tastensymbole |
| Schriftart und Schriftschnitt | Schriftfamilie, normal/fett/kursiv |
| Schriftgröße | 6–48 Punkt; Fenster passt sich dem Inhalt an |
| Darstellung zurücksetzen | Standardfarben, Hintergrunddeckkraft und Schrift wiederherstellen |

Die Transparenz betrifft ausschließlich den Hintergrund. Die Schrift und die
Symbole behalten ihre Deckkraft; ausgegraute Gerätetasten zeigen wie bisher,
dass eine Bedienung aktuell nicht möglich ist.

**Rechtsklick → Angezeigte Werte:** Feuchte, Zielfeuchte, Temperatur, PM2,5 und IAI
einzeln ein- oder ausblenden. Standardmäßig sind die ersten vier eingeschaltet.
Alle Darstellungsoptionen werden nach Bestätigung sofort angewendet und gespeichert.
Abbrechen verwirft die Auswahl. Die Vorschau speichert keine Änderungen.
Diese Optionen verändern das Widget; die Lichttaste oben steuert das reale Gerät.

## Update installieren

Das neue ZIP im Ordner Downloads speichern. Die vorhandenen Abhängigkeiten reichen aus:

```bash
pkill -x airctrl-desklet
cd ~/Downloads
unzip -o airctrl-desklet-0.3.3.zip
cd airctrl-desklet
bash install.sh
env -u QT_QPA_PLATFORM ~/.local/bin/airctrl-desklet
```

Qt wählt das Backend anhand der verfügbaren Sitzung. Der vorher empfohlene
erzwungene Wayland-Start wurde entfernt, nachdem am Benutzerrechner kein passender
Wayland-Socket erreichbar war. Diagnose (F1) zeigt unter „Plattform“ das tatsächlich
verwendete Qt-Backend und zusätzlich den Sitzungstyp an.

Für eine erste Installation unter Fedora vorher:

```bash
sudo dnf install -y gcc-c++ cmake make qt6-qtbase-devel qt6-qtsvg openssl-devel json-devel python3 unzip
```

Installation für deinen Benutzer unter `~/.local`. Menüeintrag: **Philips AirControl**.
Das Quellverzeichnis in Downloads wird nach der Installation nicht mehr benötigt.
Python dient ausschließlich zum Schreiben des Menüeintrags; beide laufenden
Programme sind C++.

## Bedienung

Die acht Tasten entsprechen der Reihenfolge in deiner Vorlage. Eine Taste mit
mehreren Optionen öffnet ein Auswahlmenü; erst eine Auswahl schreibt zum Gerät.

| Taste von links | Funktion | Übertragene Werte |
|---|---|---|
| 1 · Ein/Aus | Gerät ein- oder ausschalten | `pwr` als String `"1"` / `"0"` |
| 2 · Kindersicherung | Sperren / entsperren | `cl` als Boolean |
| 3 · Automatik | Automatik, Allergen oder Nacht | `mode`: `P`, `A`, `S`; Nacht zusätzlich `om=s` |
| 4 · Lüfter | Stufe 1, 2, 3 oder Turbo | `mode=M`, `om`: `1`, `2`, `3`, `t` |
| 5 · Luftfeuchte | Ziel 40, 50, 60 oder 70 % | `rhset` als Integer |
| 6 · Licht | Lichtring dimmen; Geräteanzeige ein/aus | `aqil` als Integer; `uil` als String |
| 7 · 2-in-1 | Reinigung oder Reinigung mit Befeuchtung | `func`: `P` / `PH` |
| 8 · Timer | Aus oder 1–12 Stunden bis zum Ausschalten | `dt` als Integer |

Die Zuordnung der Bedienelemente folgt der
[Philips-Kurzanleitung](https://dam.versuni.com/m/7a41a3a71a50dea9/original/Quick-start-guide-Philips-Series-2000i-2-in-1-air-purifier-and-humidifier-AC2729_11.pdf).
Die CoAP-Modi folgen der auf dem AC2729/10 erprobten
[Integration von Denaun](https://github.com/Denaun/philips-airpurifier-coap/blob/d1583b2840f562b5c5380222a98ff27b1fe17ca6/custom_components/philips_airpurifier_coap/fan.py).
Weitere Datentypen und Wertebereiche sind in
[py-air-control](https://github.com/rgerganov/py-air-control/blob/master/pyairctrl/airctrl.py)
dokumentiert. Filterzähler werden nicht zurückgesetzt; die Uhrtaste dient dem Timer.

**Verschieben:** Eine Wertezeile oder freie Fläche mit der linken Maustaste ziehen.
Unter Wayland übernimmt der Fenstermanager das Ziehen. Eine feste Startposition
kann das Widget dort nicht wiederherstellen. Unter X11 wird die Position gespeichert;
dort gibt es zusätzlich „Position festlegen …“ für X/Y-Koordinaten.
„Position sperren“ verhindert das Ziehen an den Werten; das Menü bleibt zugänglich.

**Rechtsklick:** Darstellung, angezeigte Werte, Aktualisieren, Verbindung/Autostart,
Diagnose, Position sperren und Beenden. Im Menü steht auch der Verbindungsstatus.
Ein kurzer Linksklick auf einen Wert öffnet dasselbe Menü.
Bei aktivem Widget funktioniert auch die Menütaste oder **Umschalt+F10**.
**F5:** Status aktualisieren. **F1:** Diagnose öffnen.
**Diagnose:** Der erste Reiter zeigt für jeden empfangenen Geräte-Tag den
unveränderten Wert und eine deutsche Bedeutung. Der zweite Reiter erklärt
Verbindung, Qt-Plattform, Sitzung und Backend. Unter „Rohdaten“ bleibt der
vollständige bisherige Bericht samt JSON erhalten. **Bericht kopieren** übernimmt
Erklärungen und Rohdaten gemeinsam.

Erklärt werden unter anderem Betriebsmodus, Kindersicherung, Licht, Luftwerte,
Timer, Wasserstatus, Filterzähler, Firmware, WLAN und Gerätekennungen. Die
Filterzähler werden als Restbetriebsstunden beschrieben, wie sie auch von
[py-air-control](https://github.com/rgerganov/py-air-control#usage-in-the-local-network)
interpretiert werden. Das Programm rundet sie nicht in Kalenderfristen um.
Unbekannte oder nicht zuverlässig dokumentierte Felder wie `dtrs`, `rddp`,
`aqit_ext` und fremde künftige Tags werden entsprechend gekennzeichnet. Der
Rohwert bleibt erhalten; insbesondere aus einem unbekannten `err`-Code wird kein
gesicherter Wartungsalarm abgeleitet. Die bekannten Grundzuordnungen folgen der
[Philips-CoAP-Integration](https://github.com/kongo09/philips-airpurifier-coap/blob/master/custom_components/philips_airpurifier_coap/const.py).

Regelmäßige Statusabfragen lassen die Gerätetasten bedienbar. Gesperrt sind sie
beim Ausführen eines Steuerbefehls bis zur Rückmeldung, offline und in der Vorschau.
Bei aktivierter Kindersicherung bleibt nur die Taste zum Entsperren verfügbar.
Bei ausgeschaltetem Gerät bleiben Ein/Aus und Kindersicherung verfügbar.
Für im empfangenen Status fehlende Fähigkeiten werden keine Befehle angeboten.

## Verbindung und Rückmeldungen

Nach einer Änderung liest die Anwendung den Status neu. Nur die Rückmeldung
ändert die angezeigten Werte. Schreibbefehle werden nie automatisch wiederholt.
Empfangene Messwerte lösen ihrerseits keine Schreibbefehle aus.

Ein während einer Statusabfrage geklickter Befehl wartet auf deren erfolgreichen
Abschluss. Die ältere Antwort gilt nicht als Bestätigung dieses Befehls; erst
der neue Status nach dem Schreiben aktualisiert die Anzeige. Schlägt die laufende
Abfrage fehl oder wird das Widget beendet, wird der vorgemerkte Befehl verworfen.
Er wird bei einer späteren Wiederverbindung nicht nachträglich ausgeführt.

Die Abfrage läuft standardmäßig zehn Sekunden nach Abschluss der vorherigen
Anfrage; das Intervall ist unter Einstellungen von 5 bis 300 Sekunden wählbar.
Jede CoAP-Anfrage hat zehn Sekunden Zeit, der gesamte Backend-Prozess höchstens
25 Sekunden. Eine fehlgeschlagene Statusabfrage wird einmal nach einer Sekunde
wiederholt. Pro Widget läuft höchstens ein Backend-Prozess gleichzeitig.

Bei Verbindungsverlust bleiben letzte Messwerte abgeblendet sichtbar.
„Keine Verbindung“, Zeitpunkt des letzten Empfangs und Fehlerdetails stehen
im Tooltip der Werte und unter Diagnose; das Kontextmenü zeigt den Verbindungsstatus.
Die Ursache des zuvor am Benutzergerät aufgetretenen Verbindungsabbruchs ist
weiterhin nicht geklärt. Dieses Update ändert die Tastensperre und die Reihenfolge
von Abfragen und Bedienbefehlen in der GUI. Das CoAP-Backend ist unverändert.

## Desktop und Autostart

Dies ist ein eigenständiges Qt6-Desktopwidget. Cinnamons native Desklets verwenden
JavaScript/CJS; dieses Programm startet über das Anwendungsmenü oder den Autostart.

**Wayland:** Das Widget ist ein gewöhnliches rahmenloses Anwendungsfenster.
Das Ziehen verwendet [Qt QWindow::startSystemMove](https://doc.qt.io/qt-6/qwindow.html#startSystemMove),
weil feste globale Fensterpositionen unter Wayland nicht gesetzt werden können.
Die Einordnung auf der Desktop-Ebene und das Wiederherstellen einer Startposition
werden deshalb nicht zugesichert. Die Erkennung berücksichtigt die Wayland-Sitzung
auch dann, wenn Qt über XWayland gestartet wurde. Die neue Mausbedienung wurde hier
automatisiert unter Qt-Offscreen geprüft; ein echter Cinnamon/Wayland-Test steht aus.

**X11:** Unter Cinnamon/X11 verwendet das rahmenlose Fenster DOCK + BELOW.
[Muffin ordnet diese Kombination oberhalb des Desktops und unterhalb normaler Fenster ein](https://github.com/linuxmint/muffin/blob/cde5c6210e7d8c4239a8d4ba410bfdaeb12e0caa/src/x11/window-x11.c).
Es werden keine Bildschirmränder reserviert. Auch für diesen Fenstertyp ersetzt
die Quellcodeprüfung keinen Test in einer echten Cinnamon-Sitzung.

Unter **Rechtsklick → Verbindung und Autostart → Bei der Anmeldung starten** wird ein eigener Eintrag
unter `${XDG_CONFIG_HOME:-~/.config}/autostart/airctrl-desklet.desktop` angelegt.
Der Installer aktiviert den Autostart nicht selbst. Einstellungen liegen unter
`~/.config/AirControl/airctrl-desklet.conf`; ein Update erhält sie.

## Weitere Startmöglichkeiten

```bash
~/.local/bin/airctrl-desklet --window
~/.local/bin/airctrl-desklet --host 192.168.77.5
~/.local/bin/airctrl-desklet --reset-position
~/.local/bin/airctrl-desklet --demo
```

`--reset-position` wirkt nur bei einer Plattform, die globale Fensterpositionen
unterstützt. Unter Wayland entscheidet der Fenstermanager über die Startposition.

Eine Instanzsperre verhindert doppelte Abfragen durch mehrere normale Instanzen.
Vor einem erneuten Start die laufende Instanz beenden. Die Vorschau verändert
keine Einstellungen und kommuniziert nicht mit dem Gerät.

## Bauen, testen und Diagnose

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/airctrl-desklet
```

Qt Creator kann die oberste `CMakeLists.txt` direkt öffnen. `airctrl-desklet` und
`airctrl-backend` müssen nach Build bzw. Installation nebeneinander liegen.
Die Oberfläche startet das Backend über `QProcess` ohne Shell.

```bash
# Status unmittelbar über das mitgelieferte Backend prüfen
./build/airctrl-backend -H 192.168.77.5 status -J

# Echte Qt-Oberfläche als PNG rendern, ohne Gerätezugriff
QT_QPA_PLATFORM=offscreen ./build/airctrl-desklet --screenshot vorschau.png
```

Die automatisierten Prüfungen verwenden ein simuliertes Backend. Angaben zu
Build, Testergebnissen, Bildprüfung und Grenzen stehen in `VALIDATION.md`.
Die C++-Statusabfrage und erste GUI-Messwerte hat der Benutzer am realen Gerät
bestätigt. Die zusätzlichen Panelbefehle wurden hier mit simulierten Antworten
geprüft; ein Test am physischen Gerät steht aus.

## Entfernen und Lizenz

Widget beenden und `bash uninstall.sh` im Quellverzeichnis ausführen. Das entfernt
Programme, Menüeintrag, Icon und eigenen Autostart. Persönliche Einstellungen bleiben.

MIT, siehe `LICENSE`. CoAP-Code: C++-Port von
[betaboon/aioairctrl](https://github.com/betaboon/aioairctrl), Original-Commit
`c97640b054c14c0d02739fdfa2564cd85f8216ea`, Copyright 2020 betaboon.
Qt, OpenSSL und nlohmann/json werden über installierte Bibliotheken eingebunden.
Das ZIP enthält keine Qt-Binärdateien. Die Tastensymbole werden im Qt-Code gezeichnet. Für die Werte werden
die auf deinem System installierten Schriftarten verwendet.
