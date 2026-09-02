# Validierung – Version 0.3.2

Datum: 2026-09-01.

## Build

- Linux x86-64, Ubuntu 24.04.
- GCC 13.3.0, C++17, Release-Build.
- Qt 6.8.3: Core, Gui, Widgets und Test.
- OpenSSL 3.0.13, nlohmann/json 3.12.0.
- Oberfläche und Backend erfolgreich kompiliert.
- Keine Compilerwarnungen aus dem UI-Code bei `-Wall -Wextra -Wpedantic`.
- Qt-SDK und zusätzliche Entwicklungsdateien wurden lokal zum Testen verwendet;
  sie sind nicht im ZIP enthalten. Die Fedora-Anleitung verwendet Systempakete.

## Automatisierte Prüfungen

QtTest/CTest, mit Qt-Offscreen-Plattform:

```text
Totals: 31 passed, 0 failed, 1 skipped, 0 blacklisted
100% tests passed out of 1
```

Die 31 erfolgreichen QtTest-Einträge umfassen 29 Testfälle plus Initialisierung
und Aufräumen. Der zusätzliche native X11-Test wurde unter Offscreen übersprungen:

| Prüffall | Ergebnis |
|---|---|
| Statusabfrage ohne unbeabsichtigtes Schreiben | OK |
| `pwr` als String, `rhset` als Integer, anschließendes Lesen | OK |
| Ungültige Zielfeuchte ohne Backend-Aufruf zurückweisen | OK |
| Prozessfehler und anschließende Wiederherstellung | OK |
| Ungültiges JSON zurückweisen | OK |
| Fehlgeschlagene Statusabfrage einmal automatisch wiederholen | OK |
| Fehlgeschlagene Schreibbefehle nicht automatisch wiederholen | OK |
| Anhalten verhindert ausstehende Lesewiederholung | OK |
| Timeout und Unterdrückung überlappender Abfragen | OK |
| Fehlendes Backend anzeigen | OK |
| Messwerte, fehlende Felder, Sperren der Steuerung bei Offline-Zustand | OK |
| Kompaktes Layout, nur acht Tasten und Werte, Schriftgrößen 10/24/48 ohne Abschneiden | OK |
| Hintergrundalpha 0/50/100 %; Vordergrund bleibt deckend | OK |
| Transparenz über echtes Kontextmenü ändern und speichern, ohne Gerätezugriff | OK |
| Vorschau ohne Abfragen oder Schreibzugriffe | OK |
| Einstellungen speichern/laden, Autostart anlegen/entfernen | OK |
| Neue Panelbefehle: Datentypen, kombinierte Moduswerte und Rücklesen | OK |
| Ungültige Timer-, Licht-, Sperr- und Moduswerte zurückweisen | OK |
| Echte Zielfeuchte-Menüauswahl und Kindersicherung ein/aus | OK |
| Rechtsklick über Hintergrund, Leiste, Werten, aktiver und deaktivierter Taste; nur ein Menü bei zusätzlichem Kontext-Ereignis | OK |
| Kurzer Linksklick auf Werte öffnet Menü auch bei Positionssperre | OK |
| Ziehen unter Offscreen verändert und speichert Position, ohne Menü oder Backend-Aufruf | OK |
| Positionsdialog setzt und speichert X/Y außerhalb von Wayland | OK |
| Simulierte Wayland-Sitzung: rahmenloses normales Fenster, genau eine native Verschiebeanfrage, Sperre und Menü, keine X/Y-Aktion | OK |
| Verzögerte Statusabfrage: alle acht Tasten bleiben durchgehend aktiv; Klick während Folgeread wird genau einmal geschrieben und frisch bestätigt | OK |
| Vorgemerkten Befehl bei Prozessfehler, ungültigem JSON und Timeout verwerfen; keine verspätete Ausführung nach Wiederverbindung | OK (3 Fälle) |
| Stoppen verwirft vorgemerkten Befehl auch bei anschließendem Neustart der Steuerung | OK |
| Native X11-Fensterattribute | Übersprungen: kein X11-Display |

Für Version 0.1.0 wurden zusätzlich die 14 vorhandenen UDP-Integrationstests aus
`aioairctrl-cpp` gegen `airctrl-backend` ausgeführt: alle erfolgreich. Das Backend
ist in Version 0.3.2 unverändert; diese Suite wurde nicht erneut ausgeführt. Darunter
IPv4/IPv6, echte UDP-Loopback-Kommunikation, Python-Referenzverschlüsselung,
Statusbeobachtung, CoAP-Fehler, Timeouts und typisierte Schreibwerte. Die
Async-Untertests verwendeten den bereits gebauten C++-Testtreiber des ursprünglichen
Bibliotheksprojekts. Diese zusätzliche Suite gehört weiterhin zum ursprünglichen
`aioairctrl-cpp`-Paket.

Die fünf eingebundenen C++-Protokolldateien wurden byteweise mit dem zuvor am
Benutzergerät erfolgreichen C++-Port verglichen: unverändert.

## Oberfläche und Installation

- Die echte Qt-Oberfläche wurde im Vorschau-Modus als PNG gerendert und visuell
  geprüft: `vorschau.png`. Keine überlappenden oder abgeschnittenen Bedienelemente
  bei der geprüften Standardskalierung (287 × 85 Pixel).
- Die Vorschau zeigt 55 % Luftfeuchte, Ziel 50 %, 24 °C und PM2,5 = 1.
  Sie greift nicht auf das reale Gerät zu; der Vorschaustatus steht nur im Tooltip und Kontextmenü.
- `install.sh` wurde erneut mit einem separaten absoluten Testpräfix ausgeführt:
  Build und Ersetzen der vorherigen Installation erfolgreich; beide Programme vorhanden.
- Der Versionsaufruf der installierten GUI meldet `airctrl-desklet 0.3.2`.
- Shellsyntax von Installations- und Deinstallationsskript geprüft.

## Layout und Einstellungen

Die vorherige Kreisanzeige ist entfernt. Der Benutzer wünscht nur Tastenleiste und
Werte; alle weiteren Funktionen liegen im Kontextmenü. Das Standardlayout wurde
als 287 × 85 Pixel gerendert und visuell geprüft. Große Schrift vergrößert die
Inhaltsfläche automatisch. Verfügbare Werte lassen sich im Kontextmenü auswählen.

Hintergrundfarbe, Vordergrundfarbe, Transparenz, komplette QFont-Einstellung und
Werteauswahl wurden gespeichert und erneut geladen. Der Darstellungstest sendet
ein Kontextmenü-Ereignis an eine Wertezeile, setzt die Transparenz im Dialog und
prüft den gespeicherten Wert sowie das Ausbleiben von Backend-Aufrufen. Neue
Maustests ergänzen vollständige Press/Release-Folgen auf mehreren Oberflächen.
Die Alpha-Prüfung verwendet echte Qt-Bilddaten: Hintergrund 0/50/100 % transparent,
Text weiterhin deckend. Qt-Offscreen unterstützt das Rendern dieser Alpha-Werte;
die Komposition mit einem echten Cinnamon-Desktophintergrund wurde nicht getestet.

Die Steuerbefehle sind gegenüber 0.2.0 unverändert. Quellen für deren Zuordnung
stehen in `README.md`. Keine Wartungsalarme werden aus undokumentierten Statusbits
abgeleitet.

## Bedienbarkeit während Statusabfragen

Bis 0.3.1 hing die Freigabe der Gerätetasten am allgemeinen `busy`-Zustand des
Controllers. Jede Hintergrundabfrage sperrte deshalb die gesamte Tastenleiste
bis zum Empfang der Antwort. Version 0.3.2 sperrt die Tasten stattdessen während
einer vom Benutzer ausgelösten Änderung und bis zu deren Rückmeldung. Offline-,
Vorschau-, Kindersicherungs- und geräteabhängige Sperren bleiben erhalten.

Der Controller merkt genau einen während einer laufenden Abfrage eingehenden
Steuerbefehl vor und startet ihn nach einer gültigen Antwort. Diese ältere Antwort
wird nicht als Bestätigung an die Oberfläche geliefert. Nach dem Schreiben folgt
wie bisher eine neue Statusabfrage. Es laufen keine Backend-Prozesse parallel.
Ein Fehler oder Stoppen verwirft den vorgemerkten Befehl.

Der neue Integrationstest hält die simulierte Leseantwort gezielt zurück und
zeichnet alle `EnabledChange`-Ereignisse der acht Tasten auf. Während der normalen
Abfrage tritt keine Deaktivierung auf. Ein anschließender echter Mausklick während
einer weiteren Abfrage führt zur Folge Lesen → einmal Schreiben → neu Lesen.
Die Tasten bleiben bis zu dieser neuen Antwort gesperrt. Weitere Prüfungen decken
den Abbruch bei Prozessfehler, ungültiger Antwort, Timeout und Stoppen ab.

## Menü und Wayland

Der vorherige direkte Kontextmenü-Test deckte die tatsächliche Mausfolge nicht ab.
Seit Version 0.3.1 werden Rechtsklicks in den Ereignisfiltern aller Widgetflächen behandelt.
Ein kurzer Linksklick auf einen Wert öffnet dasselbe Menü; Mausbewegung über der
Ziehschwelle löst stattdessen das Verschieben aus. Menüs werden nach Abschluss
der Maustastenfreigabe geöffnet, zusätzliche native Kontext-Ereignisse dedupliziert.

Eine Wayland-Sitzung wird über Qt-Plattformname, `XDG_SESSION_TYPE` und gegebenenfalls
`WAYLAND_DISPLAY` erkannt. Auch bei XWayland werden die X11-DOCK/BELOW-Attribute
in einer Wayland-Sitzung nicht gesetzt. Das Widget fordert das Verschieben über
[QWindow::startSystemMove](https://doc.qt.io/qt-6/qwindow.html#startSystemMove) während
der gehaltenen Maustaste an. Es setzt dort keine feste globale Fensterposition.
Darstellungsänderungen überschreiben zuvor gespeicherte X11-Koordinaten nicht.

Der automatisierte Wayland-Prüffall setzt den Sitzungstyp unter Offscreen und
ersetzt ausschließlich den Aufruf an den Compositor durch einen Zähler. Er prüft
die Ereignisführung, Fensterflags, Menüzugriff, Positionssperre und Speicherung.
Er beweist keine tatsächliche Verschiebung oder Popup-Anzeige unter Cinnamon/Wayland.

## Noch nicht hier geprüft

Die nur für X11 verwendete Kombination DOCK + BELOW wurde anhand des
[Muffin-Quellcodes](https://github.com/linuxmint/muffin/blob/cde5c6210e7d8c4239a8d4ba410bfdaeb12e0caa/src/x11/window-x11.c)
geprüft: `get_standalone_layer` ordnet sie `META_LAYER_BOTTOM` zu. Die Reihenfolge
in `src/meta/common.h` ist DESKTOP < BOTTOM < NORMAL. Das erklärt die Änderung
gegenüber dem früheren DESKTOP-Fenstertyp, ersetzt aber keinen Sitzungstest.

Keine laufende Cinnamon/Muffin- oder Wayland-Sitzung in der Testumgebung. Menü-Popups,
interaktives Verschieben unter Wayland, Desktop-Ebene,
„Desktop anzeigen“, virtuelle Arbeitsflächen, Tray und Autostart müssen in der
Benutzersitzung erprobt werden. Bei Bedarf `--window` benutzen.

Kein Zugang zum physischen Philips-Gerät: Die Statusabfrage des zugrunde liegenden
C++-Ports hat der Benutzer bereits bestätigt. Neue GUI-Bedienung und
Schreibbefehle wurden hier gegen simulierte Antworten geprüft.

Der Screenshot des Benutzers zeigt auch bereits empfangene GUI-Messwerte und einen
anschließenden Verbindungsfehler. Dessen genaue Ursache ist noch unbekannt.
Version 0.3.2 verwendet weiterhin zehn Sekunden je CoAP-Anfrage, einen Prozess-Watchdog von
25 Sekunden und eine einmalige Wiederholung fehlgeschlagener Statusabfragen.
Die neue Diagnose zeigt den tatsächlichen Backend-Fehler. Eine Behebung der
AT-SPI-Startmeldungen ist nicht Teil dieses Updates.
