# AirCtrl-Desklet

**Dein Philips-Luftreiniger. Direkt auf dem Linux-Desktop.**

C++17 · Qt 6 · Lua 5.4 · lokale CoAP-Kommunikation · MIT · Version **v1.02**

[English](README.en.md) · [Bedienung](docs/USER_GUIDE.de.md) · [Entwicklung](docs/DEVELOPMENT.md) · [Änderungen](CHANGELOG.md)

![AirCtrl-Desklet mit dunklem Hintergrund, Gerätetasten, Statussymbolen und Messwerten](docs/images/desklet-dark.png)

*Echter Screenshot von Jürgen: Cinnamon unter X11, angepasste Farben und Schrift,
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
  Luftreinigung/2-in-1 und Abschalttimer.
- Feuchte, Zielfeuchte, Temperatur, PM2,5 und IAI einzeln einblendbar.
- Aktive Modus- und Wartungssymbole aus bestätigten Statusmeldungen.
- Dauerhafte CoAP-Beobachtung statt zyklischer Einzelabfragen.
- Datenalter in Sekunden und separate Alarmglocke.
- Filtervorwarnungen, Quittierung, Desktop-Benachrichtigungen und optionaler Ton.
- Farben, Hintergrundtransparenz, Schrift, Fensterdekoration und Autostart im Kontextmenü.
- Diagnose mit deutschen Feldbeschreibungen, Hexcodes und kopierbarem Gesamtbericht.
- Sichere Lua-Automatik für Status-, Verbindungs-, Alarm- und Zeitereignisse,
  einschließlich Tag/Nacht-Zeitplänen.

![Helle Standarddarstellung des Widgets](docs/images/desklet-light.png)

*Helles Standardlayout, echte Qt-Demodarstellung ohne Geräteverbindung.
Ausgegraute Gerätetasten sind in der Demo absichtlich nicht bedienbar.*

## Schnellstart auf Fedora

Voraussetzungen: C- und C++17-Compiler, CMake ≥ 3.16, Qt ≥ 6.2 (Core/Gui/Widgets/DBus),
OpenSSL Crypto, nlohmann/json ≥ 3.9 und Python 3 für den Installer.

```bash
sudo dnf install -y gcc-c++ cmake make qt6-qtbase-devel qt6-qtsvg \
  openssl-devel json-devel python3 dejavu-sans-fonts

# Im entpackten oder geklonten Projektverzeichnis:
bash install.sh
~/.local/bin/airctrl-desklet --demo
```

Die Vorschau schließen. Danach die IP-Adresse des eigenen Geräts verwenden:

```bash
~/.local/bin/airctrl-desklet --host 192.0.2.10
```

`192.0.2.10` ist **nur ein Dokumentationsbeispiel**. Die tatsächliche Adresse
anschließend unter **Rechtsklick → Verbindung und Autostart** dauerhaft speichern.
`--host` gilt zunächst für diesen Start. Der historische Standard in v1.01 bleibt
aus Kompatibilitätsgründen erhalten; er ist keine automatische Geräteerkennung.

Installation unter `~/.local`, ohne `sudo` für das Installationsskript.
Menüeintrag: **Philips AirControl**. Bestehende Einstellungen bleiben erhalten.
Weitere Distributionen: [Build und Installation](docs/DEVELOPMENT.md).
Update und Entfernen: [Bedienungsanleitung](docs/USER_GUIDE.de.md).

## Bedienung

| Aktion | Bedienung |
|---|---|
| Kontextmenü | Rechtsklick; alternativ Menütaste oder Umschalt+F10 |
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
Die Automatik ist nach Installation zunächst ausgeschaltet. Das mitgelieferte
Beispiel schaltet täglich um 22:00 Uhr auf Nacht und um 07:00 Uhr auf Tag:

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
`airctrl.set { ... }` verwendet dieselbe Positivliste und Bestätigungslogik wie
die Gerätetasten. Pro Zeitplantermin gibt es höchstens einen Schaltversuch;
bereits passende Zustände erzeugen keinen Netzwerkbefehl.

Lua 5.4.9 wird aus dem geprüften offiziellen Quellstand eingebettet. Die Sandbox
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

| Umgebung | Stand |
|---|---|
| Philips AC2729/10 | Empfang und Steuerung am Gerät von Jürgen erprobt |
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
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev
python3 scripts/check_repository.py
```

Presets benötigen CMake ≥ 3.21 und Ninja. Klassischer Build ohne Presets:
[Entwicklungsleitfaden](docs/DEVELOPMENT.md). [Beiträge](CONTRIBUTING.md) ·
[Architektur und Protokollgrenzen](docs/ARCHITECTURE.md)

## Menschen, KI und Herkunft

| Beteiligter | Rolle |
|---|---|
| **Jürgen Sievers** | Projektinitiator, Product Owner und Maintainer; Anforderungen, Bedienkonzept, Prioritäten, Gerätetests und Freigaben |
| **OpenAI Codex** | KI-Entwicklungspartner; gemeinsame C++-/Qt-/Lua-Implementierung, Protokollanalyse, Fehlersuche, Tests und Dokumentation |
| **betaboon** | Autor des Python-Projekts `aioairctrl`, Grundlage des mitgelieferten C++-Backends |

Entstanden im gemeinsamen, iterativen Entwickeln — vom ersten funktionierenden
Statusabruf bis zum alltagstauglichen Desktopwidget. Codex wird transparent als
KI-Unterstützung genannt, nicht als menschlicher Maintainer oder Supportkontakt.
Entscheidungen und Veröffentlichung liegen bei Jürgen. [Credits](AUTHORS.md)

## Lizenz und Unabhängigkeit

[MIT](LICENSE). Die ursprünglichen MIT-Hinweise von betaboon bleiben erhalten.
[Herkunft und Abhängigkeiten](THIRD_PARTY_NOTICES.md). Systembibliotheken und
Schriftarten haben eigene Lizenzbedingungen und sind nicht im Quellarchiv enthalten.

Unabhängiges Community-Projekt, kein offizielles Produkt von Philips, Versuni oder
OpenAI. Produktnamen beschreiben die Kompatibilität. Keine Cloudanmeldung, aber
auch kein Ersatz für sichere Netzwerkgrenzen: den Geräteport nicht ins Internet weiterleiten.

Für den ersten Upload: [GitHub vorbereiten und veröffentlichen](docs/GITHUB_SETUP.md).
