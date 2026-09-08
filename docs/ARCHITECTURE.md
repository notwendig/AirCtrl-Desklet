# Architektur und Zustandsmodell

AirCtrl-Desklet ist eine eigenständige Qt-Widgets-Anwendung, keine Cinnamon-
JavaScript-Erweiterung. Die Oberfläche bleibt deutsch; C++-Bezeichner sind englisch.

| Baustein | Zuständigkeit |
|---|---|
| `src/main.cpp` | Anwendung, Kommandozeile, Demo und Vorschau |
| `src/desklet.*` | Fenster, Kontextmenü, Werte, Einstellungen und Diagnosefenster |
| `src/panel.*` | Acht Gerätetasten und ihre bestätigten Zustände |
| `src/emblems.*` | Statussymbole und modellbezogene Filterhinweise |
| `src/alerts.*` | Datenalter, Warnungsidentitäten, Quittierung und Benachrichtigung |
| `src/diagnostics.*` | Rohwert-Erklärungen, Hexcodes und Kopierbericht |
| `src/preferences.*` | Benutzereinstellungen und Autostart |
| `src/automation.*` | Eingebettete Lua-Sandbox, Ereignisse, Zeitpläne und Skriptprotokoll |
| `examples/automation.lua` | Kanonische Editorvorlage und kommentierte Lua-/Statusreferenz |
| `src/controlvalues.*` | Gemeinsame Positivliste und Kodierung erlaubter Steuerwerte |
| `src/controller.*` | Eine I/O-Sitzung, Fristen, Befehlszuordnung und Wiederverbindung |
| `third_party/aioairctrl` | Separates CLI und C++-Implementierung des Philips-CoAP-Protokolls |
| `third_party/lua` | Verifizierter offizieller Lua-5.4.9-Quellstand |
| `tests/` | Qt-Oberflächen-/Controllerprüfungen, Fake-Backend, lokaler UDP-Simulator |

## Empfang und Schalten

Ein langlebiger Backend-Prozess hält genau einen UDP-Socket und einen über
`/sys/dev/sync` initialisierten Protokollzustand. Er liefert Status und Schaltergebnisse als
getypte JSON-Zeilen. Gültige Statuspakete aktualisieren den Empfangszeitpunkt;
reine ACKs und fehlerhafte Zeilen tun dies nicht.

Die konfigurierte Geräteadresse bleibt ein Host-String. Das Backend löst ihn mit
`getaddrinfo(AF_UNSPEC)` auf und unterstützt dadurch IPv4, IPv6 sowie DNS-/mDNS-
Hostnamen. Protokollschema und UDP-Port werden getrennt behandelt.

Für einen Benutzerbefehl wird die laufende Observe-Anfrage sauber abgemeldet.
Danach läuft der Control-Aufruf nacheinander über denselben Client, UDP-Socket
und synchronisierten Sendezähler; anschließend wird Observe auf demselben Socket wieder angemeldet.
Die Annahme ist nicht mit einer bestätigten Zustandsänderung gleichzusetzen: erst
die nächste Statusmeldung bestätigt die Änderung. Es gibt keine automatische
Schreibwiederholung und bei einem Schaltfehler keine Neusynchronisierung.

Bleiben Statusmeldungen bis zum 90-Sekunden-Timeout aus, endet der Backend-Prozess.
Dadurch wird der alte Socket geschlossen. Nach der Wiederverbindung öffnet ein
neuer Prozess genau einen neuen Socket und synchronisiert den Protokollzustand neu.
F5, geänderte Geräteeinstellungen und fatale Prozessfehler können denselben
kontrollierten Neustart ausdrücklich ebenfalls auslösen.

Der anfängliche Status darf bis zu 60 s nach der Anmeldung benötigen; die
90-s-Frist gilt für das Ausbleiben weiterer Statusmeldungen. Wiederverbindungspause
und erneuter Anlauf kommen hinzu. Im Gerätetest vom 2026-09-08 wurden 90,0 s bis
zur Abmeldung, weitere 9,7 s bis zum neuen Sync und 36,4 s von der Anmeldung bis
zum neuen Status beobachtet: 136,1 s Datenpause insgesamt.

### Synchronisierung, Zähler und Verschlüsselung

Ein neuer UDP-Socket allein ist noch keine Philips-Synchronisierung. Der Client
sendet nach dem Öffnen ausdrücklich `/sys/dev/sync`. Die Antwort initialisiert
seinen Sendezähler; vor jedem Control-Paket wird dieser um eins erhöht. Ein
Schaltbefehl im Modus `session` löst keinen zusätzlichen Sync aus.

Der achtstellige Hex-Präfix jeder verschlüsselten Nachricht bestimmt deren
AES-128-CBC-Schlüssel und IV: `MD5("JiangPan" + Präfix)` als 32 ASCII-Zeichen
in Großbuchstaben, erste Hälfte als Schlüssel, zweite als IV. Zusätzlich wird
die SHA-256-Prüfsumme des Nachrichtenkörpers geprüft. Eingehende Meldungen
bringen ihren eigenen Präfix mit. Es besteht somit kein unveränderter
AES-Schlüssel für die ganze Socket-Lebensdauer. Im Schaltmitschnitt läuft der
Sendezähler von `0x36A2D909` bis `0x36A2D919` fortlaufend weiter.

Die [Paketbelege](PROTOCOL_VALIDATION_2026-09-08.md) unterscheiden ausdrücklich
Observe-Neuanmeldung, Socketwechsel und neue Synchronisierung. Observe ist
kein garantierter periodischer Herzschlag; bei unverändertem Zustand ist eine
Sendepause zulässig ([RFC 7641, Abschnitt 4.3.1](https://www.rfc-editor.org/rfc/rfc7641.html#section-4.3.1)).

Lua läuft im GUI-Prozess und erhält nur kopierte JSON-/Ereignisdaten. Ein
`airctrl.set`-Auftrag geht durch dieselbe Feldprüfung, Ein-Befehl-Sperre und
Statusbestätigung wie ein Klick. Zeitpläne speichern ihre ausgeführte
Terminidentität; nach einem Neustart wird nur der jüngste fällige Tag-/Nacht-
Zustand berücksichtigt. Datei-, Betriebssystem-, Paket- und Debug-Bibliotheken
werden weder geöffnet noch als API angeboten.

Die Power-Taste bleibt bedienbar, startet aber keinen zweiten bereits laufenden
Schreibauftrag. Ein Schreibfehler ist ein anderer Zustand als ein ausgefallener
Empfänger. Letzte Messwerte dürfen sichtbar bleiben, werden jedoch als veraltet
kenntlich gemacht.

## Alarme und Wissen

Das Datenalter verwendet eine monotone Zeitbasis. Die Standardgrenzen 45/90 s
gehören zur lokalen Anzeige, nicht zur Philips-Firmware.
Alarmidentitäten verhindern wiederholte Benachrichtigungen für denselben Zustand;
Verschärfung und Wiederauftreten bleiben eigenständige Ereignisse.

Für den AC2729 sind A3/HEPA, C7/Aktivkohle und F1/Befeuchtungsdocht separat
zugeordnet. Die 120-h-Vorwarnung ist eine lokale Produktentscheidung.
Unbekannte `err`-Bits werden nicht zerlegt. `0xC054` allein ist kein gesicherter
Nachweis eines bestimmten Wartungsalarms. Rohdaten bleiben unverändert.

`dtrs` wird als empirischer Hinweis auf verbleibende Timer-Minuten beschrieben:
Nach `dt=6` folgten am AC2729/10 `dtrs=360` und etwa 60 s später `359`.
Die Deutung ist nicht herstellerseitig bestätigt. Das Feld bleibt ausschließlich
ein Rohstatusfeld und wird weder zur Befehls-Positivliste noch zu Alarmregeln ergänzt.

## Desktop-Grenzen

X11 und Wayland werden getrennt behandelt. Wayland erlaubt dem Client nicht
dieselbe freie Positionierung und Desktop-Ebene wie X11. Session-Variablen und
Qt-Plattform können voneinander abweichen; die Diagnose zeigt beides.
Interaktive Tests auf einem echten Desktop bleiben notwendig.

## Bewusst nicht enthalten

Keine Cloud-Anmeldung, keine Telemetrie, kein automatisches Zurücksetzen von
Gerätewartung, kein Firmware-Update und keine automatischen Eingriffe aufgrund
einer Warnung ohne eine ausdrücklich aktivierte lokale Lua-Regel. Das Projekt
ist keine universelle Philips-Geräteintegration.
