# AirCtrl-Desklet

**Dein Philips-Luftreiniger. Direkt auf dem Linux-Desktop.**

C++17 · Qt 6 · Lua 5.4 · TCP-Server/Clients · MIT · Version **v2.00 Stable**

[English](README.en.md) · [Bedienung](docs/USER_GUIDE.de.md) · [Entwicklung](docs/DEVELOPMENT.md) · [C++-API](docs/CPP_API.md) · [Änderungen](CHANGELOG.md)

![AirCtrl-Desklet mit dunklem Hintergrund, Gerätetasten, Statussymbolen und Messwerten](docs/images/desklet-dark.png)

*Echter Screenshot des Clients: Cinnamon unter X11, angepasste Farben und Schrift,
fünf Messwerte. Kein Design-Mockup.*

AirCtrl-Desklet ist ein schlankes Desktopwidget für den **Philips AC2729/10**.
Es verbindet die wichtigsten Gerätetasten mit aktuellen Messwerten, sichtbaren
Wartungshinweisen und einer verständlichen Verbindungsdiagnose. Die Steuerung
erfolgt im lokalen Netzwerk; das Programm benötigt kein Philips-Cloudkonto.

Es ist eine eigenständige Qt-Anwendung — **kein Cinnamon-JavaScript-Desklet**.
Das Verhalten auf der Desktop-Ebene hängt vom Fenstermanager ab.
Die Oberfläche ist derzeit deutschsprachig.

## Funktionen

- Acht Tasten: Ein/Aus, Kindersicherung, Automatik, Lüfter, Zielfeuchte, Licht,
  Luftreinigung/2-in-1 und Abschalttimer. Ein mindestens 800 ms langer Druck
  auf die Timer-Taste ruft serverseitig die Lua-Funktion `on_long_timer()` auf.
- Feuchte, Zielfeuchte, Temperatur, PM2,5 und IAI einzeln einblendbar.
- Aktive Modus- und Wartungssymbole aus bestätigten Statusmeldungen.
- Ein dauerhafter `airctrl-server` als einziger AC2729-Teilnehmer und einzige
  Lua-Laufzeit. Desklet, Skripteditor und `airctrl-client` verwenden seine TCP-Schnittstelle.
- Eine Ping/Pong-Überwachung erkennt auch eine still abgerissene TCP-Verbindung
  ohne Schaltvorgang und verbindet das Desklet automatisch erneut.
- Genau eine UDP-I/O-Sitzung für alle Clients; fester Quellport und CoAP-
  Keepalive halten die Host-Firewall offen. Vor einem vollständigen Neuaufbau
  versucht der Server eine Observe-Neuanmeldung auf derselben Sitzung.
- Datenalter in Sekunden und separate Alarmglocke.
- Filtervorwarnungen, Quittierung, Desktop-Benachrichtigungen und optionaler Ton.
- Farben, Hintergrundtransparenz, Schrift, Fensterdekoration und Autostart im Kontextmenü.
- Diagnose mit deutschen Feldbeschreibungen, Hexcodes und kopierbarem Gesamtbericht.
- Serverseitige Lua-Automatik für Status-, Verbindungs-, Alarm- und
  Zeitereignisse einschließlich vollständiger `between`-Tag/Nacht-Regeln;
  exklusiver Editor auf den Clients.
- Manuelle Betriebsänderungen sperren die Lua-Automatik serverweit. Die langsam
  blinkende Power-Taste zeigt dies auf allen Clients und gibt sie mit einem Klick
  wieder frei. Licht und Kindersicherung beeinflussen die Automatik nicht.

![Helle Standarddarstellung des Widgets](docs/images/desklet-light.png)

*Helles Standardlayout, echte Qt-Demodarstellung ohne Geräteverbindung.
Ausgegraute Gerätetasten sind in der Demo absichtlich nicht bedienbar.*

## Schnellstart auf Fedora

Server-Voraussetzungen: C- und C++17-Compiler, CMake ≥ 3.16, OpenSSL Crypto,
nlohmann/json ≥ 3.9 und Threads – **kein Qt**. Matplotlib ist nur für die optionale
Status-PDF erforderlich. Der Client benötigt Qt ≥ 6.2
(Core/Gui/Widgets/DBus/Network). Python 3 wird vom
Installer verwendet.

```bash
# Auf dem Rechner mit dem Luftreiniger (nur Server, kein Qt nötig):
sudo dnf install -y gcc-c++ cmake ninja-build openssl-devel json-devel python3 python3-matplotlib
bash install.sh --server

# Auf dem Desktop-Rechner (Desklet und Kommandozeilen-Client):
sudo dnf install -y gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtsvg \
  python3 dejavu-sans-fonts
bash install.sh --client
$HOME/.local/bin/airctrl-desklet --demo
```

`bash install.sh` ohne Rollenoption installiert beide Komponenten auf demselben
Rechner. `-s` und `-c` sind die Kurzformen; beide Optionen können gemeinsam
angegeben werden. Standardmäßig landet der Server in `/usr/local/bin`, Client
und Desklet in `$HOME/.local/bin`. Ein anderer Präfix wird mit
`--prefix /absoluter/pfad` für die ausgewählten Rollen gesetzt. Das Skript selbst
wird ohne `sudo` gestartet und fordert Root-Rechte nur für die Serverinstallation
und die erstmalige Systemkonfiguration an.
Der Installer erzeugt außerdem `compile_commands.json` im Projektordner, damit
clangd/VSCodium auch generierte Header wie `airctrl_version.hpp` korrekt findet.
Nach einer bereits geöffneten Sitzung genügt **clangd: Restart language server**
oder einmal **Developer: Reload Window**.

Die Geräteadresse steht ausschließlich in `/etc/airctrld.cfg`:

```ini
[server]
listen_address=0.0.0.0
port=5680

[logging]
status_file=/var/log/airctrl.log

[automation]
enabled=false

[device]
host=AC2729-10
port=5683
local_port=5680
initial_status_ms=120000
idle_ms=90000
keepalive_ms=20000
observe_refreshes=1
cancel_grace_ms=300
```

Jeder gültige, vom Gerät bestätigte Status wird an `/var/log/airctrl.log`
angehängt. Die erste Zeile enthält die Feldnamen, danach folgt je Status eine
CSV-Zeile mit UTC-Zeitstempel und allen Werten. Boolean-Werte stehen als `0/1`;
später von einer Firmware ergänzte Felder bleiben in `_extra_json` vollständig
erhalten. Da das Protokoll auch `DeviceId` und `ProductId` enthalten kann, hat
die Datei absichtlich nur Modus `0640`. Der Installer erhält vorhandene Daten
und richtet eine Größenrotation bei 10 MiB ein.

Numerische und boolesche Statusfelder werden mit eigener beschrifteter Achse
und eigenem Titel in eine mehrseitige PDF gezeichnet, vier Diagramme pro Seite:

```bash
/usr/local/bin/airctrl-plot /var/log/airctrl.log "$HOME/airctrl-status.pdf"
```

Textfelder und Gerätekennungen bleiben im CSV erhalten, werden aber nicht auf
eine numerische Achse gezwungen. Ohne Argumente verwendet das Programm
`/var/log/airctrl.log` und schreibt `airctrl-status.pdf` in das aktuelle Verzeichnis.

Im Desklet wird unter **Rechtsklick → Verbindung und Autostart** nur der
AirControl-Server eingetragen, standardmäßig `localhost` und TCP-Port `5680`; auf einem anderen Rechner dessen Hostname oder IP-Adresse.
Der Client kennt weder Gerätehostname noch UDP-Port.

Serverinstallation unter `/usr/local`, Clientinstallation unter `$HOME/.local`, ohne
`sudo` vor dem Installationsskript. Menüeintrag: **Philips AirControl**.
Bestehende Einstellungen bleiben erhalten.
Der Installer aktiviert außerdem den systemd-Benutzerdienst `airctrl-server`.
Ohne verfügbare systemd-Benutzersitzung muss `airctrl-server` separat gestartet werden.
Weitere Distributionen: [Build und Installation](docs/DEVELOPMENT.md).
Update und Entfernen: [Bedienungsanleitung](docs/USER_GUIDE.de.md).

Server und Kommandozeilen-Clients (`server` durch den tatsächlichen Hostnamen oder die IP-Adresse ersetzen):

```bash
systemctl --user status airctrl-server.service
airctrl-client --host server --port 5680 status
airctrl-client --host server --port 5680 watch
airctrl-client --host server --port 5680 set pwr=1
airctrl-client --host server set mode=S om=s uil=0
airctrl-client --host server refresh
```

Nur `airctrl-server` enthält die Philips-CoAP-Anbindung. Alle Clients sprechen
zeilenbasiertes JSON über TCP, standardmäßig mit `localhost:5680`. Dieses Protokoll
hat keine eigene Anmeldung oder Verschlüsselung; Port 5680 darf in der Firewall
nur für vertrauenswürdige Rechner im lokalen Netz freigegeben werden. Zusätzlich
muss der feste lokale Geräteport UDP 5680 ausschließlich für Pakete von der
AC2729-Adresse freigegeben sein. Das verhindert, dass verzögerte Observe-
Benachrichtigungen nach Ablauf des UDP-Connection-Trackings verworfen werden.

## Bedienung

| Aktion | Bedienung |
|---|---|
| Kontextmenü | Per Maus ausschließlich Rechtsklick; alternativ Menütaste oder Umschalt+F10 |
| Verschieben | An Messwerten, Statussymbolen oder Kreisen ziehen; Positionssperre vorher lösen |
| Fensterrahmen | Kontextmenü → Fensterdekoration ausblenden |
| Diagnose | F1 oder Kontextmenü → Diagnose / Gerätedaten |
| Empfang neu starten | F5; im Normalbetrieb nicht erforderlich |
| Alarmdetails | Linksklick auf einen Statuskreis |
| Bericht kopieren | Diagnose → Bericht kopieren oder Strg+Umschalt+C |

Power bleibt anklickbar: **orange** ohne Verbindung, **weiß** bei ausgeschaltetem
und **grün** bei eingeschaltetem Gerät. Während eines laufenden Befehls werden
keine zusätzlichen Power-Befehle gesendet.

Links zählt der Kreis Sekunden seit dem letzten gültigen Datenpaket: standardmäßig
grün unter 45 s, gelb ab 45 s und rot ab 90 s. Rechts stehen Haken oder Alarmanzahl.
Die Kreise messen bei Standardschrift 26 px und wachsen mit der Schriftgröße.

## Lua-Automatik

Unter **Rechtsklick → Lua-Automatik** öffnet sich der integrierte Skripteditor.
Beim Öffnen fordert er eine serverweite Sperre an und lädt danach das aktuelle
Skript vom Server. Beim Speichern wird der Text zum Server zurückgesendet, dort
geprüft, atomar gespeichert und neu geladen. Gleichzeitig kann genau ein Client
bearbeiten; Abbrechen oder ein Verbindungsabbruch gibt die Sperre frei.

Die Automatik ist nach Installation zunächst ausgeschaltet und läuft nach der
Aktivierung ausschließlich im `airctrl-server` – auch ohne geöffnetes Desklet. Das mitgelieferte
Beispiel enthält als Kommentare die vollständige Ereignis-, Statusfeld- und
Steuerwertreferenz. Aktiv schaltet es täglich um 22:00 Uhr auf Nacht und um
07:00 Uhr auf Tag:

```lua
airctrl.schedule {
    name = "nacht", at = "22:00",
    days = {1, 2, 3, 4, 5, 6, 7},
    set = { mode = "S", om = "s", uil = "0" }
}

airctrl.schedule {
    name = "tag", at = "07:00",
    set = { mode = "P", uil = "1" }
}
```

`on_event(event)` erhält `startup`, `time`, `connected`, `disconnected`,
`status`, `alarm` und `command`. Statusereignisse enthalten den vollständigen
bestätigten Zustand in `event.status` sowie Änderungen in `event.changed`.
Ein mindestens 800 ms langer Druck auf die Timer-Taste ruft zusätzlich die
globale Serverfunktion `on_long_timer()` ohne Argumente auf.
`airctrl.set { ... }` verwendet im Server dieselbe Positivliste,
Gerätewarteschlange und Bestätigungslogik wie die Gerätetasten. Pro Zeitplantermin gibt es höchstens einen Schaltversuch;
bereits passende Zustände erzeugen keinen Netzwerkbefehl.

Lua 5.4.9 wird aus dem geprüften offiziellen Quellstand ausschließlich in den
Qt-freien Server eingebettet. Die Sandbox
stellt nur Basis-, Tabellen-, String-, Mathematik- und UTF-8-Funktionen bereit:
keine API für beliebige Datei-, Netzwerk- oder Prozesszugriffe und kein `io`,
`os`, `package`, `debug`, `dofile`, `loadfile` oder `load`. Nur `airctrl.set`
darf erlaubte Werte an das konfigurierte Gerät senden. Zusätzlich gelten 8 MiB
Speicher und 200.000 VM-Instruktionen je Aufruf. Das schützt vor vielen Fehlern,
macht fremde Skripte aber nicht automatisch vertrauenswürdig.
[Lua-API und Beispiele](docs/LUA_AUTOMATION.md)

### Wartung

| Code | Bedeutung | Reststundenzähler |
|---|---|---|
| A3 | HEPA-Filter wechseln | `fltsts1` |
| C7 | Aktivkohlefilter wechseln | `fltsts2` |
| F1 | Befeuchtungsdocht wechseln, nicht reinigen | `wicksts` |
| F0 | Vorfilter / Befeuchtungselement reinigen | `fltsts0` und bekannte Reinigungscodes |

Für AC2729 zeigt das Desklet je Filter eine gelbe Vorwarnung bei **1–120
Restbetriebsstunden**, bei **0 h** einen roten Alarm. **120 h ist eine lokale
Desklet-Grenze, keine gesichert dokumentierte Philips-Frühwarnschwelle.**
Unbekannte Fehlerbits werden nicht geraten; `0xC054` allein erzeugt keinen Alarm.
Quittieren verändert keine Geräteeinstellung und setzt keinen Filterzähler zurück.
Die Geräteanzeige bleibt maßgeblich.

## Diagnose, die Rohwerte erhält

![Diagnose mit Feldbeschreibungen, Hexwerten und Kopierbutton, ausschließlich Beispieldaten](docs/images/diagnostics-demo.png)

*Aus der tatsächlichen Qt-Oberfläche mit neutralen Testdaten gerendert.
Keine persönlichen Gerätekennungen im öffentlichen Bild.*

Gerätewerte, Verbindung, Roh-JSON und Kopierbericht haben eigene Reiter.
„Bericht kopiert“ bestätigt die lokale Übergabe. Einfügen: Strg+V, im Terminal
Strg+Umschalt+V; Mittelklick, falls Qt die PRIMARY-Auswahl unterstützt.
Vor dem Teilen Gerätekennungen, Namen, Netzwerkadressen und lokale Pfade entfernen.
[Sicher melden](SECURITY.md)

## Kompatibilität und Testgrenzen

Die beiden Gerätemitschnitte vom **8. September 2026** bestätigen den v1.04-
Ablauf: **148 gültige Statusmeldungen**, **17 von 17 angenommene Schaltbefehle**
mit passender nächster Statusmeldung nach **45–97 ms** und kein neuer UDP-Port
oder Sync beim Schalten.

Observe wird beim Schalten kurz ab- und wieder angemeldet, während der Socket
bestehen bleibt. Eine 65,8-s-Pause bei ausgeschaltetem Gerät führt zu keinem
Neustart. Der damalige 90-s-Neuaufbau erzeugte in einem Fall insgesamt 136,1 s
ohne frische Daten.
[Paketbelege vom 8. September](docs/PROTOCOL_VALIDATION_2026-09-08.md)

Der Mitschnitt vom **13. September 2026** bestimmt die Ursache der aktuellen
Wiederverbindungsschleife: Drei gültige Statusmeldungen erreichen nach 35–55 s
den Serverhost, werden dort aber sofort mit ICMP „administratively prohibited“
abgewiesen. Kommt die erste Meldung schon nach 25 s, bleibt derselbe Socket über
elf Meldungen und eine Sendepause von 77 s stabil. Deshalb verwendet der Server
jetzt UDP 5680 als konfigurierbaren Quellport, einen 20-s-Keepalive, eine längere
erste Antwortfrist und eine Observe-Neuanmeldung vor dem vollständigen Neuaufbau.
[Firewallbefund und Härtung vom 13. September](docs/PROTOCOL_VALIDATION_2026-09-13.md)

| Umgebung | Stand |
|---|---|
| Philips AC2729/10 | Gemeinsamer UDP-Port und 17 Schaltungen bestätigt; neuer Mitschnitt belegt eine lokale Firewallablehnung verzögerter Observe-Pakete |
| Server/Clients v1.06 | TCP-Mehrclientbetrieb, zentrale `/etc/airctrld.cfg`, Keepalive, fester Quellport und Observe-Erneuerung lokal geprüft; die gehärtete Fassung muss nach dem Einspielen noch am Gerät bestätigt werden |
| Fedora 44 / Cinnamon / X11 (`xcb`) | Vom Nutzer bestätigt, einschließlich Diagnoseexport von v1.01 |
| Wayland | Angepasste Fensterbehandlung vorhanden; kein vollständiger nativer Desktop-Test bestätigt |
| Andere Philips-Modelle | Nicht freigegeben; Modellzuordnungen und Befehle können abweichen |

Qt wählt normalerweise das passende Fenster-Backend selbst. Wayland nicht
erzwingen, wenn kein entsprechender Sitzungssocket verfügbar ist.
Die lokale Prüfhistorie steht in [VALIDATION.md](VALIDATION.md), die vorbereitete
CI in [.github/workflows/ci.yml](.github/workflows/ci.yml). Ein vorbereiteter Workflow
ist noch kein erfolgreicher GitHub-Lauf.

## Entwickeln und beitragen

```bash
cmake --preset debug-server
cmake --build --preset debug-server --parallel 2
cmake --preset debug-client
cmake --build --preset debug-client --parallel 2
ctest --preset debug-client
python3 scripts/check_repository.py
```

Presets benötigen CMake ≥ 3.21 und Ninja. Klassischer Build ohne Presets:
[Entwicklungsleitfaden](docs/DEVELOPMENT.md). [Beiträge](CONTRIBUTING.md) ·
[Architektur und Protokollgrenzen](docs/ARCHITECTURE.md)

## Menschen, KI und Herkunft

| Beteiligter | Rolle |
|---|---|
| **Projektinitiator (Rolle)** | Product Owner und Maintainer; Anforderungen, Bedienkonzept, Prioritäten, Gerätetests und Freigaben |
| **OpenAI Codex** | KI-Entwicklungspartner; gemeinsame C++-/Qt-/Lua-Implementierung, Protokollanalyse, Fehlersuche, Tests und Dokumentation |
| **betaboon** | Autor des Python-Projekts `aioairctrl`, Grundlage der internen C++-Geräteanbindung im Server |

Entstanden im gemeinsamen, iterativen Entwickeln — vom ersten funktionierenden
Statusabruf bis zum alltagstauglichen Desktopwidget. Codex wird transparent als
KI-Unterstützung genannt, nicht als menschlicher Maintainer oder Supportkontakt.
Entscheidungen und Veröffentlichung liegen beim Maintainer. [Credits](AUTHORS.md)

## Lizenz und Unabhängigkeit

[MIT](LICENSE). Die ursprünglichen MIT-Hinweise von betaboon bleiben erhalten.
[Herkunft und Abhängigkeiten](THIRD_PARTY_NOTICES.md). Systembibliotheken und
Schriftarten haben eigene Lizenzbedingungen und sind nicht im Quellarchiv enthalten.

Unabhängiges Community-Projekt, kein offizielles Produkt von Philips, Versuni oder
OpenAI. Produktnamen beschreiben die Kompatibilität. Keine Cloudanmeldung, aber
auch kein Ersatz für sichere Netzwerkgrenzen: den Geräteport nicht ins Internet weiterleiten.

Für den ersten Upload: [GitHub vorbereiten und veröffentlichen](docs/GITHUB_SETUP.md).
