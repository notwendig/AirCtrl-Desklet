# Architektur und Zustandsmodell

AirCtrl-Desklet ist eine eigenständige Qt-Widgets-Anwendung, keine Cinnamon-
JavaScript-Erweiterung. Die Oberfläche bleibt deutsch; C++-Bezeichner sind englisch.

| Baustein | Zuständigkeit |
|---|---|
| `src/client/desklet_main.cpp` | Desklet-Anwendung, Kommandozeile, Demo und Vorschau |
| `src/client/desklet.*` | Fenster, Kontextmenü, Werte, Einstellungen und Diagnosefenster |
| `src/client/panel.*` | Acht Gerätetasten und ihre bestätigten Zustände |
| `src/client/emblems.*` | Statussymbole und modellbezogene Filterhinweise |
| `src/client/alerts.*` | Datenalter, Warnungsidentitäten, Quittierung und Benachrichtigung |
| `src/client/diagnostics.*` | Rohwert-Erklärungen, Hexcodes und Kopierbericht |
| `src/client/preferences.*` | Benutzereinstellungen und Autostart |
| `src/client/automation.*` | Eingebettete Lua-Sandbox, Ereignisse, Zeitpläne und Skriptprotokoll |
| `examples/automation.lua` | Kanonische Editorvorlage und kommentierte Lua-/Statusreferenz |
| `src/common/version.hpp.in` | Einzige, Qt-freie gemeinsame Versionsvorlage |
| `src/client/controlvalues.*` | Qt-seitige Positivliste und Kodierung erlaubter Steuerwerte |
| `src/client/ipc.*` | Qt-seitige TCP-Standardwerte für Desklet und CLI |
| `src/server/main.cpp` | Qt-freier POSIX-TCP-Server, Befehlswarteschlange und Geräte-I/O |
| `src/client/controller.*` | Reiner Desklet-Client, IPC-Zustand und Befehlsbestätigung |
| `src/client/cli_main.cpp` | Kommandozeilen-Client für Status, Beobachtung, Schalten und F5-Ersatz |
| `third_party/aioairctrl` | Nur vom Server verwendete C++-Implementierung des Philips-CoAP-Protokolls |
| `third_party/lua` | Verifizierter offizieller Lua-5.4.9-Quellstand |
| `tests/` | Qt-Oberflächen-/Clientprüfungen, Fake-Server, Mehrclient- und UDP-Simulator |
| `Doxyfile`, `docs/CPP_API.md` | Englische C++-Schnittstellenreferenz und Erzeugungsanleitung |

Die Produktionsquellen sind damit physisch getrennt: Der Server-Unterbaum und
sein CMake-Zweig enthalten keinerlei Qt-Abhängigkeit. Der Server verwendet
C++17, POSIX-Sockets, nlohmann/json, OpenSSL und Threads. Der Client-Unterbaum
enthält keine Geräte-/CoAP-Quelle; seine Qt-Helfer gehören ausschließlich zum
Target `airctrl_client_common`. Gemeinsam ist nur die generierte Versionsnummer.

## Server, Clients und Geräte-I/O

`airctrl-server` ist der einzige
installierte Prozess mit Zugriff auf `aioairctrl`. Er hält genau einen UDP-Socket
und einen über `/sys/dev/sync` initialisierten Protokollzustand. Weder Desklet
noch Lua noch `airctrl-client` öffnen UDP oder kontaktieren den AC2729 direkt.

Die Clients verbinden sich mit dem im Client eingestellten TCP-Endpunkt,
standardmäßig `nadhh:5680`. Nachrichten sind auf 1 MiB begrenzte, mit Zeilenumbruch abgeschlossene JSON-
Objekte. Status- und Serverzustände werden an alle verbundenen Clients verteilt.
Eine Schaltantwort geht ausschließlich an den Client, der ihre Kennung erzeugt hat.
Mehrere Clientaufträge werden serverweit serialisiert; vor dem nächsten Versuch
muss Observe mindestens einen neuen Gerätestatus geliefert haben.

Bei einer Voll- oder Serverinstallation (`install.sh --server`) richtet das Skript
den systemd-Benutzerdienst `airctrl-server.service` ein. `install.sh --client`
installiert dagegen ausschließlich Desklet und CLI. Das Desklet startet keinen Server. Das Schließen oder Abstürzen eines Clients beendet
den Server und dessen Geräte-I/O nicht. Der Server hält den letzten Status nur
als Cache; während eines Verbindungsfehlers wird er neuen Clients nicht als
frischer Status ausgegeben.

Vor der Verteilung hängt der Server jeden nichtleeren Objektstatus an das
CSV-Protokoll `/var/log/airctrl.log` an. Der erste Status legt die alphabetisch
sortierten Spalten fest; spätere unbekannte Schlüssel werden verlustfrei in der
reservierten Spalte `_extra_json` gesammelt. Ein Prozess-Lock und `O_APPEND`
schützen vollständige Datensätze, die Kopfzeile wird nach `copytruncate` selbst
wiederhergestellt. Das installierte headless Python-Programm ermittelt numerische
Spalten aus diesem stabilen Schema und erzeugt eine mehrseitige PDF mit vier
Diagrammen pro Seite.

Listenadresse, TCP-Port, Geräteadresse, UDP-Port und Gerätefristen liest der
Server aus `/etc/airctrld.cfg`. Die Geräteadresse bleibt ein Host-String. Ausschließlich der Server
löst ihn mit `getaddrinfo(AF_UNSPEC)` auf und unterstützt dadurch IPv4, IPv6 sowie
DNS-/mDNS-Hostnamen. Protokollschema und UDP-Port werden getrennt behandelt.

Für einen Benutzerbefehl wird die laufende Observe-Anfrage sauber abgemeldet.
Danach läuft der Control-Aufruf nacheinander über denselben Client, UDP-Socket
und synchronisierten Sendezähler; anschließend wird Observe auf demselben Socket wieder angemeldet.
Die Annahme ist nicht mit einer bestätigten Zustandsänderung gleichzusetzen: erst
die nächste Statusmeldung bestätigt die Änderung. Es gibt keine automatische
Schreibwiederholung und bei einem Schaltfehler keine Neusynchronisierung.
Wird Geräte-I/O während eines laufenden Auftrags erneuert, meldet der Server den
Ausgang als unbekannt und lässt keinen Status der neuen Sitzung als Bestätigung gelten.

Der Geräteclient bindet standardmäßig den konfigurierbaren lokalen UDP-Port 5680.
Während einer offenen Beobachtung sendet er alle 20 Sekunden ein leeres CoAP-CON,
damit zustandsbehaftete Firewalls auch in ereignisbedingten Sendepausen einen
gültigen Rückweg behalten. Die erste Statusmeldung darf bis zu 120 Sekunden
benötigen; für weitere Meldungen gilt zunächst die 90-Sekunden-Frist.

Läuft eine Statusfrist ab, registriert der Geräteclient Observe einmal mit
demselben Token und Socket neu und wartet weitere 60 Sekunden. Erst danach wird
der alte Socket nach einer kurzen Abmeldefrist geschlossen. Nach der
Wiederverbindung erstellt derselbe Serverprozess genau einen neuen Geräteclient,
öffnet einen neuen Socket und synchronisiert den Protokollzustand neu. Die TCP-
Clients bleiben verbunden. F5 und fatale Protokollfehler können denselben
kontrollierten I/O-Neustart auslösen. Änderungen an `/etc/airctrld.cfg` werden
nach einem Neustart des systemd-Benutzerdienstes wirksam.

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

Die [Paketbelege vom 8. September](PROTOCOL_VALIDATION_2026-09-08.md) unterscheiden ausdrücklich
Observe-Neuanmeldung, Socketwechsel und neue Synchronisierung. Observe ist
kein garantierter periodischer Herzschlag; bei unverändertem Zustand ist eine
Sendepause zulässig ([RFC 7641, Abschnitt 4.3.1](https://www.rfc-editor.org/rfc/rfc7641.html#section-4.3.1)).
Darum hält der Server den UDP-Firewallzustand unabhängig vom Statusstrom mit
leeren CoAP-CON-Paketen offen. Ein abgelaufener Statuszeitraum führt zuerst zu
einer Observe-Neuanmeldung mit gleichem Token und Socket; erst deren Fehlschlag
ersetzt Socket und Synchronisierung. Der [Mitschnitt vom 13. September](PROTOCOL_VALIDATION_2026-09-13.md)
belegt die zuvor nicht bekannte Firewallablehnung verzögerter Meldungen.

Lua läuft im GUI-Prozess und erhält nur kopierte JSON-/Ereignisdaten. Ein
`airctrl.set`-Auftrag geht wie ein Klick über den Controller und die TCP-Verbindung
durch dieselbe Feldprüfung, Ein-Befehl-Sperre und Statusbestätigung. Zeitpläne speichern ihre ausgeführte
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
