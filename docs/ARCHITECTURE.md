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
| `src/controller.*` | Beobachtungsprozess, Schreibprozess, Fristen und Wiederverbindung |
| `third_party/aioairctrl` | Separates CLI und C++-Implementierung des Philips-CoAP-Protokolls |
| `third_party/lua` | Verifizierter offizieller Lua-5.4.9-Quellstand |
| `tests/` | Qt-Oberflächen-/Controllerprüfungen, Fake-Backend, lokaler UDP-Simulator |

## Empfang und Schalten

Ein langlebiger `status-observe`-Prozess liefert zeilenweise JSON. Gültige
Statuspakete aktualisieren den Empfangszeitpunkt. Reine ACKs und fehlerhafte
Zeilen tun dies nicht. Reguläre Pausen zwischen Gerätepaketen starten keine
zyklische Neuabfrage.

Die konfigurierte Geräteadresse bleibt ein Host-String. Das Backend löst ihn mit
`getaddrinfo(AF_UNSPEC)` auf und unterstützt dadurch IPv4, IPv6 sowie DNS-/mDNS-
Hostnamen. Protokollschema und UDP-Port werden getrennt behandelt.

Ein Benutzerbefehl läuft in einem separaten Schreibprozess. Die Annahme eines
Schreibbefehls ist nicht mit einer bestätigten Zustandsänderung gleichzusetzen:
erst die nächste passende Statusmeldung bestätigt die Änderung. Die Beobachtung
läuft währenddessen weiter. Es gibt keine automatische Schreibwiederholung.

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
