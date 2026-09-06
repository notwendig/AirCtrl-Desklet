# Philips AirControl – Qt6-Gerätepanel v1.02

Kompaktes C++/Qt6-Desktopwidget für den Philips AC2729/10 unter
Cinnamon. Geräteadresse voreingestellt: **192.168.77.5**, UDP-Port **5683**.
Der bereits am Gerät funktionierende C++-CoAP-Code ist vollständig enthalten.

**v1.02** ergänzt eine standardmäßig ausgeschaltete Lua-Automatik für Ereignisse
und lokale Zeitpläne. Das eingebaute Beispiel schaltet Tag/Nacht; alle Aufträge
verwenden die vorhandene Feldprüfung und bestätigte Gerätesteuerung.
Änderungsübersicht: [CHANGELOG.md](../CHANGELOG.md). Prüfungen und verbleibende
Umgebungsgrenzen: [VALIDATION.md](../VALIDATION.md).

Seit Version 0.3.8 gibt es den gespeicherten Kontextmenü-Haken **Fensterdekoration
ausblenden** und eine **Code-(Hex)-Spalte** in der Diagnose. Datenalter und Alarm
erscheinen jetzt als zwei Kreise neben den Status-Emblemen; die untere Alarmleiste
entfällt. Empfang, Schaltbefehle und Alarmgrenzen bleiben unverändert.

Version 0.3.7 ergänzte einen Sekundenzähler mit Datenalter-Ampel sowie sichtbare
Warnungen und Fehler, Desktop-Benachrichtigungen und optionalen Signalton.
Grenzwerte und Benachrichtigungen sind über das Kontextmenü einstellbar.

Seit Version 0.3.6 gibt es eine Zeile mit den aktuellen Status-Emblemen zwischen
Buttonleiste und Messwerten. Symbole, Schrift und Farben bleiben skalierbar;
die erprobte Dauerbeobachtung und Gerätesteuerung sind unverändert.

Seit Version 0.3.5 ersetzen wir wiederholte Einzelabfragen durch eine dauerhafte
CoAP-Beobachtung (`status-observe`). Schaltbefehle beenden diese Beobachtung nicht.
Die im Mitschnitt beobachteten 18–19 Sekunden zwischen Meldungen lösen keinen
Offline-Wechsel mehr aus. Power bleibt wie in 0.3.4 immer anklickbar:
orange ohne Verbindung, weiß bei „aus“, grün bei „an“.

## Kompakte Oberfläche

Im Widget stehen die acht Kontrolltasten, das Statusfeld mit aktiven Emblemen und
zwei Kreisen für Datenalter/Alarme, darunter die Werte. Der linke Kreis zählt
Sekunden, der rechte zeigt einen Haken ohne Alarm oder eine Glocke mit Alarmanzahl.
Warnungen sind gelb, Fehler rot; quittierte aktive Alarme tragen ein kleines „Q“.
Tooltips und ein Klick auf die Kreise zeigen die vollständigen Alarmtexte.
Standardmäßig sind Feuchte, Zielfeuchte, Temperatur und PM2,5 sichtbar.
Das Standardlayout misst **287 × 142 Pixel** ohne Fensterdekoration bei 100 % Desktopskalierung.
Das Statusfeld reserviert bis zu zwei Reihen mit sechs Emblemen, damit auch bei
mehreren Wartungswarnungen keine Symbole überlappen und die Fenstergröße stabil bleibt.
Bei größerer Schrift wächst das Fenster mit, damit nichts abgeschnitten wird.

`vorschau.png` zeigt die echte Qt-Oberfläche, `embleme-vorschau.png` mehrere
Modi und einen ausdrücklich simulierten Wartungsfall, `power-vorschau.png` die drei
Power-Farben. In der Vorschau bleibt Power anklickbar, sendet aber keine Befehle;
alle übrigen Gerätetasten sind deaktiviert. „Vorschau“ steht im Tooltip und Menü.

## Datenalter und Alarme

Der Zähler zeigt **Sekunden seit dem letzten gültigen Statuspaket**, nicht Minuten.
Er wird jede Sekunde aktualisiert und bei jedem vollständig empfangenen, gültigen
JSON-Statuspaket auf null gesetzt – auch während der Bestätigung eines Schaltbefehls.
Ein Schreib-ACK, eine ungültige/unvollständige JSON-Zeile, ein Fehler oder ein neuer
Verbindungsversuch setzen ihn nicht zurück. Unter Linux nutzt er
[CLOCK_BOOTTIME](https://man7.org/linux/man-pages/man2/clock_gettime.2.html):
Uhrzeitkorrekturen verändern ihn nicht, Suspend-Zeit wird mitgezählt.

| Farbe | Standardgrenze |
|---|---|
| Grün · OK | 0 bis 44 Sekunden |
| Gelb · Achtung | 45 bis 89 Sekunden |
| Rot · Zu alt | Ab 90 Sekunden |
| Rot · Verbindungsfehler | Sofort bei gemeldetem Empfangsfehler, unabhängig vom Alter |
| Grau · Noch keine Daten | „—“ im Kreis, bis erstmals ein Statuspaket eingetroffen ist |

Die Grenzen lassen sich unter **Rechtsklick → Datenalter und Alarme …** ändern.
Die rote Grenze muss größer als die gelbe sein. Das verändert nur Anzeige/Alarme,
nicht den bisherigen 90-Sekunden-Timeout des Empfängers. Schaltfehler machen den
Alarmbereich rot, aber nicht den weiterhin frischen Datenzähler.

**Alarmquellen:** ausbleibende Daten, Verbindungsfehler, abgelehnte/nicht bestätigte
Schaltbefehle sowie die bekannten Wasser-, Reinigungs- und Filterwechselhinweise
aus der Emblemzeile. Keine neue Interpretation unbekannter Gerätecodes: Ein
`err=49236` allein erzeugt weiterhin keinen Alarm. Gerätewarnungen bei fehlender
Verbindung beruhen auf dem letzten bestätigten Status, nicht auf neuen Messungen.

Ein neuer Alarm löst standardmäßig **eine Desktop-Benachrichtigung** aus. Ein
optionaler zusätzlicher Signalton ist ab Werk aus. Meldung/Ton erfolgen einmal
pro Störung, erneut bei Verschärfung (Gelb → Rot) oder wenn eine zwischenzeitlich
behobene Störung wieder auftritt. Wiederholte Empfangsfehler erzeugen keinen
Alarmsturm. Die Demo erzeugt weder Desktop-Meldungen noch Töne.

**Linksklick auf einen der beiden Kreise → aktive Alarme.** Das Fenster wird laufend
aktualisiert. Alternativ: Rechtsklick → Aktive Alarme / Alarme quittieren.
Quittierung markiert andauernde Ursachen mit „(Q)“; sie bleiben farbig sichtbar.
Ein vergangener Schaltfehler verschwindet nach Quittierung oder erfolgreicher
Bestätigung eines späteren Befehls aus den aktiven Alarmen, bleibt aber als letzter
Schaltfehler in der Diagnose. Quittieren sendet keinen Befehl zum Gerät und setzt
keinen Wartungszähler zurück. Ziehen an den Kreisen verschiebt weiterhin das Widget.
Auch lange Zeiträume bleiben Sekundenwerte; die Zahl wird bei Bedarf im Kreis
verkleinert, ohne den Kreis oder das Fenster im Sekundentakt zu vergrößern.
Die Einheit „Sekunden“ steht im Tooltip und in den Alarmdetails; im kleinen Kreis
steht nur die Zahl. Beide Kreise bleiben gemeinsam anklickbar und skalieren mit
der gewählten Schriftgröße.

### Filtervorwarnung ab v1.01

| Gerätecode | Zähler | Bedeutung |
|---|---|---|
| A3 | `fltsts1` | HEPA-Filter austauschen |
| C7 | `fltsts2` | Aktivkohlefilter austauschen |
| F1 | `wicksts` | Befeuchtungsdocht austauschen, nicht reinigen |

Beim AC2729 warnt das Desklet für jeden Filter einzeln **gelb bei 1 bis 120
Restbetriebsstunden**; bei **0 Stunden wird der Alarm rot**. Die 120 h sind eine
**lokale Desklet-Vorwarngrenze, keine gesichert dokumentierte Philips-Schwelle**.
Anlass ist die Benutzerbeobachtung der wechselnden A3/C7/F1-Anzeigen bei noch
positiven Zählern; die mitgeteilten Werte 119 h und 88 h liegen in diesem Bereich.
Die genaue Bitbelegung von `err=49236` (`0xC054`) bleibt unbekannt. Der Code und
die Typkennungen `fltt1`/`fltt2` allein lösen weiterhin keinen Alarm aus.

Die Glocke zählt die Filter einzeln. Jede Warnung hat eine eigene Alarmidentität;
eine stündliche Änderung der Restlaufzeit meldet sich nicht erneut. Bei Erreichen
von 0 Stunden wird erneut gewarnt, auch nach Quittierung der Vorwarnung.
`fltsts0` ist weiterhin der Reinigungszähler (F0). Das Desklet setzt keine Zähler
zurück und löst keine Wartungsbefehle aus; die Geräteanzeige bleibt maßgeblich.
Die Displaycodes beschreibt die
[Philips-Kurzanleitung für AC2729](https://dam.versuni.com/m/36dbf77346d3da10/original/Quick-start-guide-Philips-Series-2000i-2-in-1-air-purifier-and-humidifier-AC2729_90.pdf).

Desktop-Meldungen verwenden den
[Benachrichtigungsdienst der Sitzung](https://specifications.freedesktop.org/notification/latest/protocol.html)
über QtDBus,
auch ohne Tray-Symbol. Falls der Dienst fehlt oder Meldungen unterdrückt werden,
bleiben Zähler, Alarmfarbe und Alarmdetails im Widget verfügbar. Der optionale
Signalton verwendet den Desktop-Systemton; dessen Hörbarkeit hängt von den
Audio-/Desktop-Einstellungen ab. Die Alarmkonfiguration wird lokal gespeichert.

`alarme-vorschau.png` zeigt die drei Ampelfarben und eine simulierte Gerätewarnung.

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
| Filterwechsel / Vorwarnung | Beim AC2729: `fltsts1`, `fltsts2` oder `wicksts` zwischen 0 und 120 Reststunden; lokale Grenze |
| Wasser nachfüllen | Beim AC2729 in 2-in-1: `wl=0` oder bekannter Code `err=49408` |
| Reinigung | Beim AC2729: `fltsts0=0` oder `err=49153/49155`; `wicksts` gehört zum F1-Austausch |

Betriebs-, Anzeigen-, Timer- und Wartungssymbole erscheinen nur bei bestätigtem
`pwr="1"`. Bei ausgeschaltetem oder unbekanntem Betriebszustand bleiben nur die
Verbindungsanzeige und gegebenenfalls das Schloss. Offline bleiben die letzten
Betriebssymbole abgeblendet und mit „Letzter bestätigter Zustand“ im Tooltip erhalten.
Ein noch unbestätigter Schaltbefehl ändert keine Symbole.

Die Wartungssymbole sind **Hinweise aus Statuscodes und Restlaufzeitzählern**, keine
vollständige Spiegelung aller firmwareinternen Display-Alarme. Die Legacy-Codes
sind auf eine erkannte Modellkennung AC2729 begrenzt. `err=49236` und unbekannte
Codes erzeugen allein keine Warnung. `32768` wird wegen uneinheitlicher Deutung von
Wassermangel/Tanköffnung nicht als Leerstandscode verwendet. Fehlende, ungültige
oder negative Zähler werden niemals als Null interpretiert. Geräteanzeige abgleichen;
das Widget setzt keinen Wartungszähler zurück.

Die Zuordnungen stützen sich auf die vorhandene
[Philips-CoAP-Referenzintegration](https://github.com/betaboon/philips-airpurifier/blob/master/custom_components/philips_airpurifier_coap/const.py).
Die Bedeutung der Filterwechsel- und Reinigungshinweise beschreibt auch
[Philips](https://www.philips.co.uk/c-f/XC000005727/what-filters-should-i-use-with-my-philips-air-purifier/1000).

Das Statusfeld behält beim Statuswechsel seine Höhe. Embleme folgen Schriftgröße und
Vordergrundfarbe; Wartungshinweise bleiben orange. Ihre Tooltips erklären den
Zustand und das auslösende Statusfeld. Linksklick oder Rechtsklick öffnet das
Kontextmenü; Ziehen verschiebt das Widget wie bisher. Es gibt keine zusätzlichen
Geräteabfragen und keine neuen Schaltbefehle.

## Kontextmenü und Darstellung

**Kurzer Linksklick auf einen Messwert oder Rechtsklick auf das Widget** öffnet
das Kontextmenü. Seit Version 0.3.1 werden die Maustasten direkt verarbeitet, auch über
Wertefeldern und Gerätetasten. Ziehen und kurzer Klick werden unterschieden.
Im Tray-Menü gibt es zusätzlich „Menü / Darstellung …“.

**Rechtsklick → Fensterdekoration ausblenden:**

- Haken gesetzt: keine native Titelleiste und kein Fensterrahmen.
- Haken entfernt: normale Fensterdekoration durch Qt/Fenstermanager.
- Die Auswahl wird sofort gespeichert, ohne die Geräteverbindung neu zu starten.
- Rechtsklick, Umschalt+F10 und Ziehen an den Werten/Kreisen bleiben verfügbar.

Dies betrifft nur das Hauptwidget, nicht Diagnose-, Alarm- oder Einstellungsdialoge.
Hintergrundfarbe und Transparenz werden weiterhin separat unter Darstellung geändert.
Beim ersten Update wird die bisherige Rahmenlos-/Fenster-Einstellung übernommen.
`--window` startet weiterhin ausdrücklich mit normaler Fensterdekoration.
Qt muss das Fenster bei einem [Wechsel der Fensterflags](https://doc.qt.io/qt-6/qwidget.html#windowFlags-prop)
kurz aus- und wieder einblenden. Unter X11 bleibt die gespeicherte Position erhalten;
unter Wayland entscheidet der Compositor über die Platzierung und Dekoration.

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

## Lua-Automatik

**Rechtsklick → Lua-Automatik** öffnet Editor, Aktivierung und Ladezustand. Die
Taste **Tag/Nacht-Beispiel** setzt einen Entwurf mit Nachtmodus um 22:00 Uhr und
Automatikmodus um 07:00 Uhr ein. Erst **Speichern und neu laden** übernimmt ihn.

Lua kann auf Statusänderungen, Verbindung, Alarme, Befehlsresultate und den
Minutentakt reagieren. Zeitpläne werden lokal ausgewertet. Ist das Widget beim
Termin nicht aktiv, wird beim nächsten Start nur der jüngste fällige Zustand
nachgeholt. Ein übergebener Geräteauftrag wird nicht automatisch wiederholt.

Die Automatik ist nach der Installation aus. Skripte haben keinen Datei-,
Netzwerk-, Shell- oder Prozesszugriff; Speicher und Ausführung sind begrenzt.
Die vollständige API, Ereignistabellen, erlaubten Gerätefelder und weitere
Beispiele stehen unter [Lua-Automatik](LUA_AUTOMATION.md).

## Update installieren

Die laufende Instanz über das Kontextmenü beenden. Das neue Quell-ZIP in einen
neuen, leeren Ordner entpacken; nicht ungeprüft über eine Git-Arbeitskopie schreiben.
Im enthaltenen Ordner `AirCtrl-Desklet` ausführen:

```bash
bash install.sh &&
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
Der Quellordner darf frei gewählt werden; die persönlichen Einstellungen liegen
unabhängig davon in deinem Benutzerprofil.
Zusätzlich wird jetzt das Qt6-DBus-Modul aus Qt Base zum Bauen benötigt.
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
Erklärungen und Rohdaten gemeinsam; alternativ **Strg+Umschalt+C** im Diagnosefenster.
Nach dem Klick erscheint **Bericht kopiert (… Zeichen)**. Einfügen mit **Strg+V**,
im Terminal mit **Strg+Umschalt+V**. Unterstützt Qt eine PRIMARY-Auswahl, wird auch
diese befüllt, sodass Mittelklick denselben Bericht einfügt. Der Button benötigt
keine vorherige Textmarkierung und schließt die Diagnose nicht.

Der zusätzliche Reiter **Kopierbericht** enthält den vollständigen kopierten Text
zum Prüfen und manuellen Markieren (Strg+A, Strg+C). Normales Strg+C in einem
Textreiter kopiert weiterhin nur die Auswahl. Die Diagnose wird aus Menüs erst
nach deren Schließen geöffnet, damit kein Popup-Eingabegriff bestehen bleibt.
Unter Wayland muss das Diagnosefenster aktiv sein. Kopieren erfolgt unmittelbar
beim Klick/Tastendruck, nicht über Hintergrundtimer oder externe Hilfsprogramme;
siehe [Qt-Zwischenablage](https://doc.qt.io/qt-6/qclipboard.html).
Die erfolgreiche Übergabe an eine andere Anwendung muss noch auf dem jeweiligen
Desktop geprüft werden; die lokale Rückleseprüfung ist kein Compositor-End-to-End-Test.

Seit 0.3.8 zeigt die zusätzliche Spalte **Code (Hex)** `err`, `dtrs`, `ddp`, `rddp`,
`aqit`, `aqit_ext` und `wl` hexadezimal, z.B. **49236 → 0xC054**.
Der empfangene Dezimalwert bzw. String bleibt daneben erhalten; Hexwerte stehen
auch im kopierten Erklärungsbericht. Der JSON-Reiter bleibt unverändert.
Nur nichtnegative, ganzzahlige Zahlen/Dezimalstrings werden umgerechnet, keine
Booleschen Werte, Bruchteile oder Typkennungen wie A3. Messwerte und
Filter-Restlaufzeitzähler behalten ihre bisherige Dezimaldarstellung.
Die Hexdarstellung interpretiert keine neuen Fehlerbits oder Wartungsalarme.

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

**Wayland:** Das Widget ist ein gewöhnliches Anwendungsfenster, standardmäßig rahmenlos.
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
Wird die Dekoration eingeblendet, verwendet das Programm bei aktivem Desktopmodus
ein normales BELOW-Fenster, weil der Fenstermanager Dock-Fenster üblicherweise nicht
dekoriert. Beim erneuten Ausblenden wird die bisherige Dock-Einstellung wiederhergestellt.

Unter **Rechtsklick → Verbindung und Autostart → Bei der Anmeldung starten** wird ein eigener Eintrag
unter `${XDG_CONFIG_HOME:-~/.config}/autostart/airctrl-desklet.desktop` angelegt.
Der Installer aktiviert den Autostart nicht selbst. Einstellungen liegen unter
`~/.config/AirControl/airctrl-desklet.conf`; ein Update erhält sie.

## Weitere Startmöglichkeiten

```bash
~/.local/bin/airctrl-desklet --window
~/.local/bin/airctrl-desklet --host 192.0.2.10
~/.local/bin/airctrl-desklet --reset-position
~/.local/bin/airctrl-desklet --demo
```

`192.0.2.10` ist ein Beispiel; durch die eigene Geräteadresse ersetzen.
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

Optionaler D-Bus-Integrationstest in einer eigenen, isolierten Testsitzung:

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DAIRCTRL_TEST_WITH_DBUS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Benötigt `dbus-run-session` und die Erlaubnis zum Anlegen lokaler D-Bus-Sockets.
Ohne diese Option wird nur dieser Integrationstest übersprungen; Nachrichtenaufbau
und Alarmzustandswechsel werden weiterhin ohne Desktopdienst geprüft.

Qt Creator kann die oberste `CMakeLists.txt` direkt öffnen. `airctrl-desklet` und
`airctrl-backend` müssen nach Build bzw. Installation nebeneinander liegen.
Die Oberfläche startet das Backend über `QProcess` ohne Shell.

```bash
# Status unmittelbar über das mitgelieferte Backend prüfen
./build/airctrl-backend -H 192.0.2.10 status -J

# Echte Qt-Oberfläche als PNG rendern, ohne Gerätezugriff
QT_QPA_PLATFORM=offscreen ./build/airctrl-desklet --screenshot /tmp/airctrl-demo.png
```

Die automatisierten Prüfungen verwenden ein simuliertes Backend bzw. einen
lokalen UDP-Gerätesimulator. Angaben zu Build, Testergebnissen, Bildprüfung und
Grenzen stehen in [VALIDATION.md](../VALIDATION.md).
Die C++-Statusabfrage und erste GUI-Messwerte hat der Benutzer am realen Gerät
bestätigt. Die zusätzlichen Panelbefehle wurden hier mit simulierten Antworten
geprüft. Der frühere Nutzermitschnitt bestätigt zusätzlich mehrere reale
Schaltbefehle, nicht aber jede mögliche Tasten-/Modellkombination.

## Entfernen und Lizenz

Widget beenden und `bash uninstall.sh` im Quellverzeichnis ausführen. Das entfernt
Programme, Menüeintrag, Icon und eigenen Autostart. Persönliche Einstellungen bleiben.

MIT, siehe `LICENSE`. CoAP-Code: C++-Port von
[betaboon/aioairctrl](https://github.com/betaboon/aioairctrl), Original-Commit
`c97640b054c14c0d02739fdfa2564cd85f8216ea`, Copyright 2020 betaboon.
Qt, OpenSSL und nlohmann/json werden über installierte Bibliotheken eingebunden.
Lua 5.4.9 ist mit Lizenz- und Herkunftshinweisen als Quellcode enthalten.
Das ZIP enthält keine Qt-Binärdateien. Die Tastensymbole werden im Qt-Code gezeichnet. Für die Werte werden
die auf deinem System installierten Schriftarten verwendet.
