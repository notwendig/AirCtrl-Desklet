# Philips AirControl – Qt6-Gerätepanel 0.3.6

Kompaktes C++/Qt6-Desktopwidget für den Philips AC2729/10 „Wohnzimmer“ unter
Cinnamon. Geräteadresse voreingestellt: **192.168.77.5**, UDP-Port **5683**.
Der bereits am Gerät funktionierende C++-CoAP-Code ist vollständig enthalten.

Version 0.3.6 ergänzt eine Zeile mit den aktuellen Status-Emblemen zwischen
Buttonleiste und Messwerten. Symbole, Schrift und Farben bleiben skalierbar;
die erprobte Dauerbeobachtung und Gerätesteuerung sind unverändert.

Seit Version 0.3.5 ersetzen wir wiederholte Einzelabfragen durch eine dauerhafte
CoAP-Beobachtung (`status-observe`). Schaltbefehle beenden diese Beobachtung nicht.
Die im Mitschnitt beobachteten 18–19 Sekunden zwischen Meldungen lösen keinen
Offline-Wechsel mehr aus. Power bleibt wie in 0.3.4 immer anklickbar:
orange ohne Verbindung, weiß bei „aus“, grün bei „an“.

## Kompakte Oberfläche

Im Widget stehen die acht Kontrolltasten, die aktiven Status-Embleme und darunter die Werte.
Standardmäßig sind Feuchte, Zielfeuchte, Temperatur und PM2,5 sichtbar.
Das Standardlayout misst **287 × 114 Pixel** bei 100 % Desktopskalierung.
Bei größerer Schrift wächst das Fenster mit, damit nichts abgeschnitten wird.

`vorschau.png` zeigt die echte Qt-Oberfläche, `embleme-vorschau.png` mehrere
Modi und einen ausdrücklich simulierten Wartungsfall, `power-vorschau.png` die drei
Power-Farben. In der Vorschau bleibt Power anklickbar, sendet aber keine Befehle;
alle übrigen Gerätetasten sind deaktiviert. „Vorschau“ steht im Tooltip und Menü.

## Aktuelle Display-Embleme

Die Symbole sind native Qt-Vektorgrafiken nach der vom Benutzer bereitgestellten
Philips-Legende, keine Unicode-Zeichen mit fontabhängiger Darstellung.
Nur aktive, aus dem letzten bestätigten Gerätestatus ableitbare Zustände erscheinen:

| Emblem | Statuszuordnung |
|---|---|
| Mond · Ruhemodus | `pwr="1"`, `mode="S"` |
| A im Kreis · Automatik | `pwr="1"`, `mode="P"`; `om="s"` allein bedeutet hier nicht Nacht |
| Allergen | `pwr="1"`, `mode="A"` |
| Lüfter mit Stufe / T | Manuell (`mode="M"`), `om="1"/"2"/"3"/"t"` |
| Haus mit Blatt | Nur Luftreinigung: `func="P"` |
| Haus mit Tropfen | 2-in-1: `func="PH"` |
| Schloss | Kindersicherung `cl=true` |
| Uhr mit Zahl | Eingestellter Timer `dt=1…12` Stunden, nicht die verbleibende Zeit |
| PM2.5 / IAI | Am Gerät ausgewählte Anzeige `ddp=1` / `ddp=0`; keine Deutung unbekannter Codes |
| WLAN | Aktuelle Statusverbindung; offline orange und durchgestrichen |
| Filterwechsel | Beim AC2729: `fltsts1=0` oder `fltsts2=0` |
| Wasser nachfüllen | Beim AC2729 in 2-in-1: `wl=0` oder bekannter Code `err=49408` |
| Reinigung | Beim AC2729: `fltsts0=0`, `wicksts=0` oder `err=49153/49155` |

Betriebs-, Anzeigen-, Timer- und Wartungssymbole erscheinen nur bei bestätigtem
`pwr="1"`. Bei ausgeschaltetem oder unbekanntem Betriebszustand bleiben nur die
Verbindungsanzeige und gegebenenfalls das Schloss. Offline bleiben die letzten
Betriebssymbole abgeblendet und mit „Letzter bestätigter Zustand“ im Tooltip erhalten.
Ein noch unbestätigter Schaltbefehl ändert keine Symbole.

Die Wartungssymbole sind **Hinweise aus Statuscodes und Restlaufzeitzählern**, keine
vollständige Spiegelung aller firmwareinternen Display-Alarme. Die Legacy-Codes
sind auf eine erkannte Modellkennung AC2729 begrenzt. `err=49236` und unbekannte
Codes erzeugen keine Warnung. `32768` wird wegen uneinheitlicher Deutung von
Wassermangel/Tanköffnung nicht als Leerstandscode verwendet. Fehlende, ungültige
oder negative Zähler werden niemals als Null interpretiert. Geräteanzeige abgleichen;
das Widget setzt keinen Wartungszähler zurück.

Die Zuordnungen stützen sich auf die vorhandene
[Philips-CoAP-Referenzintegration](https://github.com/betaboon/philips-airpurifier/blob/master/custom_components/philips_airpurifier_coap/const.py).
Die Bedeutung der Filterwechsel- und Reinigungshinweise beschreibt auch
[Philips](https://www.philips.co.uk/c-f/XC000005727/what-filters-should-i-use-with-my-philips-air-purifier/1000).

Die Zeile behält beim Statuswechsel ihre Höhe. Embleme folgen Schriftgröße und
Vordergrundfarbe; Wartungshinweise bleiben orange. Ihre Tooltips erklären den
Zustand und das auslösende Statusfeld. Linksklick oder Rechtsklick öffnet das
Kontextmenü; Ziehen verschiebt das Widget wie bisher. Es gibt keine zusätzlichen
Geräteabfragen und keine neuen Schaltbefehle.

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
| Vordergrundfarbe | Farbe der Zahlen, Beschriftungen und übrigen Tastensymbole; Power behält seine Zustandsfarben |
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
cd /home/juergen/Projects/Qt
unzip -o ~/Downloads/airctrl-desklet-0.3.6.zip
cd /home/juergen/Projects/Qt/airctrl-desklet
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
Dein Quellverzeichnis bleibt `/home/juergen/Projects/Qt/airctrl-desklet`.
Python dient ausschließlich zum Schreiben des Menüeintrags; beide laufenden
Programme sind C++.

## Bedienung

Die acht Tasten entsprechen der Reihenfolge in deiner Vorlage. Eine Taste mit
mehreren Optionen öffnet ein Auswahlmenü; erst eine Auswahl schreibt zum Gerät.

| Taste von links | Funktion | Übertragene Werte |
|---|---|---|
| 1 · Ein/Aus | Verbunden: umschalten; offline/unbekannt: Einschaltversuch | `pwr` als String `"1"` / `"0"` |
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
**F5:** Statusverbindung ausdrücklich neu starten. **F1:** Diagnose öffnen.
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

Die laufende Statusbeobachtung lässt die Gerätetasten bedienbar. **Power bleibt immer
aktiv.** Die übrigen Tasten sind beim Ausführen eines Steuerbefehls bis zur
Rückmeldung, offline und in der Vorschau gesperrt.
Bei aktivierter Kindersicherung bleiben Power und die Taste zum Entsperren
verfügbar; das reale Gerät kann einen Power-Befehl bei aktiver Sperre ablehnen.
Bei ausgeschaltetem Gerät bleiben Ein/Aus und Kindersicherung verfügbar.
Für im empfangenen Status fehlende Fähigkeiten werden außer Power keine Befehle angeboten.

| Power-Farbe | Bedeutung | Klick |
|---|---|---|
| Orange | Keine Verbindung oder kein gültiger aktueller `pwr`-Wert | Einmaliger Einschaltversuch (`pwr=1`) |
| Weiß | Verbunden, Gerät bestätigt `pwr="0"` | Einschalten |
| Grün | Verbunden, Gerät bestätigt `pwr="1"` | Ausschalten |

Die Farbe wird nicht optimistisch nach dem Klick geändert. Erst eine neue
Statusantwort bestätigt den Zustand. Ein Timeout setzt Power wieder auf Orange;
ein alter letzter „an“-Wert wird dann nicht weiter grün dargestellt.
Der gefüllte Kreis und das dunkle Symbol bleiben bei allen Hintergrundfarben und
auch mit transparentem Hintergrund erkennbar. Tooltips erklären den Zustand
zusätzlich als Text. Power bleibt auch während eines Befehls aktiv; weitere Klicks
werden bis zu dessen Rückmeldung ignoriert. Es werden keine Doppelbefehle vorgemerkt.

## Verbindung und Rückmeldungen

Ein langlebiger Empfänger ruft einmal `status-observe -J` auf. Jede vollständige
JSON-Zeile wird sofort verarbeitet, auch wenn Zeilen über mehrere Prozessausgaben
verteilt sind oder mehrere Meldungen zusammen eintreffen. Es gibt keinen
periodischen Neustart des Empfängers und keine zyklische Neusynchronisierung.

| Grenze | Einstellung seit 0.3.5 |
|---|---|
| Synchronisierung / erste Statusantwort | Jeweils bis zu 60 s |
| Gesamter Anlauf-Watchdog | 125 s einschließlich Startreserve |
| Keine weiteren Statusmeldungen | Backend beendet nach 90 s; zusätzlicher GUI-Watchdog nach 95 s |
| Wiederverbindung nach Fehler | Standard 10 s; unter Einstellungen 5–300 s |
| Schaltanfrage | 10 s je Anfrage, Prozess-Watchdog 25 s |
| Statusbestätigung nach angenommener Änderung | Bis zu 90 s |

Der bisher gespeicherte Intervallwert wird jetzt als **Wiederverbindungspause**
verwendet; er bestimmt nicht mehr, wie oft neue Messwerte empfangen werden.
Die Meldungsrate bestimmt das Gerät. F5 startet die Beobachtung nur auf ausdrücklichen
Benutzerwunsch neu. Ein gesunder Empfänger bleibt ansonsten bestehen.

Zum Schalten läuft höchstens ein zusätzlicher Backend-Prozess auf einem eigenen
UDP-Socket. Die Beobachtung bleibt dabei erhalten. Während der Schreibanfrage
empfangene Meldungen können die Änderung noch nicht bestätigen. Erst eine neue
Statusmeldung nach der Schreibannahme wird zur Rückmeldung verwendet.
Der angezeigte Gerätezustand wird nie optimistisch umgeschaltet.

Weitere Klicks während eines laufenden Befehls werden nicht gesammelt.
Schreibbefehle werden **nie automatisch wiederholt**, auch nicht nach einem
Timeout oder einer Wiederverbindung. Das gilt ebenfalls für den expliziten
Offline-Power-Klick, der einmal `pwr=1` versucht, ohne die erste Statusmeldung
abwarten oder die Beobachtung abbrechen zu müssen.

Ein abgelehnter oder nicht bestätigter Schaltbefehl ist getrennt vom Zustand der
Beobachtung: Ein weiterhin gültiger Empfang bleibt online. Verbindungsfehler
setzen das Widget offline; letzte Werte bleiben abgeblendet sichtbar. Die Diagnose
zeigt Empfangsphase, Zahl der Statusmeldungen, Beobachtungsstarts, Wartefristen
und den letzten Schaltfehler. So lässt sich prüfen, ob der Empfänger wirklich
dauerhaft läuft (normalerweise ein Beobachtungsstart).

Unter Linux bekommen die Backend-Kinder ein Beendigungssignal, wenn das Widget
beendet oder hart abgebrochen wird. Somit bleibt auch nach `pkill` kein dauerhafter
Empfänger zurück. Ein normaler Stopp beendet die Beobachtung zunächst über SIGTERM;
antwortet der Prozess nicht, wird er nach einer Sekunde beendet.

### Grundlage aus dem Gerätemitschnitt

Der vom Benutzer bereitgestellte Test lieferte sieben gültige Statusmeldungen
über dieselbe Beobachtung. Alle sieben entschlüsselten JSON-Objekte stimmen mit
der Terminalausgabe überein. Die Synchronisierung dauerte 3,51 ms, die erste
Statusantwort 8,46 s. Weitere Meldungen hatten teilweise 18–19 s Abstand.
Nach dem Beenden sendete das Gerät noch an geschlossene UDP-Ports.

Diese Messungen begründen den Wechsel von kurzlebigen Einzelabfragen mit
10-s-Frist zur Dauerbeobachtung. Die fehlgeschlagenen Widget-Abfragen selbst
waren nicht im Mitschnitt enthalten; eine Behebung sämtlicher möglicher
Netzwerkprobleme wird daher nicht behauptet. Der anschließende Benutzermitschnitt
von 0.3.5 bestätigt am echten Gerät eine 6 Minuten 25 Sekunden lange Beobachtung,
39 gültige Statusmeldungen und 13 erfolgreich zurückgemeldete Schaltbefehle ohne
Neuanmeldung. Die Emblemzeile von 0.3.6 wurde hier mit Qt-Offscreen getestet;
physische Wartungsalarme konnten nicht ausgelöst oder überprüft werden.
Der CoAP-Protokollcode selbst ist unverändert. AT-SPI-Meldungen der
Desktop-Bedienungshilfen sind nicht Gegenstand dieses Updates.

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
