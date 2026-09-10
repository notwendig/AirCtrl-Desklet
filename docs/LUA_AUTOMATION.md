# Lua-Automatik

[Projektübersicht](../README.md) · [Architektur](ARCHITECTURE.md) · [Sicherheit](../SECURITY.md)

AirCtrl-Desklet 1.06 enthält Lua 5.4.9 als eingebettetes Automatikmodul. Es kann
auf bestätigte Gerätezustände und lokale Ereignisse reagieren oder zu festen
Uhrzeiten schalten. Nach der Installation ist es **deaktiviert**.

Öffnen: **Rechtsklick → Lua-Automatik**. Der Dialog zeigt den Skriptpfad, einen
Editor, den Ladezustand und die Taste **Tag/Nacht-Beispiel**. Erst **Speichern
und neu laden** prüft den Text, schreibt die Datei und übernimmt die Aktivierung.
Ein Syntax- oder API-Fehler bleibt im Dialog und erscheint bei aktivierter
Automatik außerdem als roter Widget-Alarm und in der Diagnose.

Die mitgelieferte Datei `examples/automation.lua` ist zugleich die im Editor
eingesetzte Vorlage. Ihre Kommentare führen alle Ereignisse, bekannten
Statusfelder, erlaubten Steuerwerte und deren Bedeutung auf. Der Build erzeugt
die eingebettete Vorlage direkt aus dieser Datei; es gibt keine zweite Kopie.

## Tag und Nacht

```lua
airctrl.schedule {
    name = "nacht",
    at = "22:00",
    days = {1, 2, 3, 4, 5, 6, 7},
    set = { mode = "S", om = "s", uil = "0" }
}

airctrl.schedule {
    name = "tag",
    at = "07:00",
    days = {1, 2, 3, 4, 5, 6, 7},
    set = { mode = "P", uil = "1" }
}
```

`at` ist lokale Rechnerzeit im Format `HH:MM`. Die Wochentage sind Montag `1`
bis Sonntag `7`; ohne `days` gilt der Termin täglich. `catch_up` ist standardmäßig
`true`: Startet das Widget nach dem Termin, wird nur der **jüngste** fällige
Zeitplan berücksichtigt. Mit `catch_up=false` gilt die Regel ausschließlich in
der exakten Minute. Zwei Zeitpläne dürfen sich an denselben Wochentagen nicht zur
gleichen Uhrzeit überschneiden.

Ein Termin wird dauerhaft als behandelt gespeichert, sobald sein Auftrag an die
Gerätesteuerung übergeben wurde. Ein fehlgeschlagener Geräteauftrag wird nicht
automatisch wiederholt. Ist die Steuerung vor der Übergabe noch belegt, prüft der
nächste Minutenimpuls den Termin erneut. Entspricht der bestätigte Status bereits
allen Zielwerten, wird der Termin ohne Netzwerkbefehl abgeschlossen.

## Ereignisse

Eine optionale globale Funktion erhält eine Tabelle:

```lua
function on_event(event)
    if event.type == "status" and event.changed.rh then
        airctrl.log("info", "Neue Feuchte: " .. event.status.rh .. " %")
    end
end
```

| `event.type` | Felder und Zeitpunkt |
|---|---|
| `startup` | Nach erfolgreichem Laden; `event.time` enthält lokale Datums-/Zeitfelder. |
| `time` | Einmal je Minute; `iso`, `date`, `time`, `year`, `month`, `day`, `weekday`, `hour`, `minute`. |
| `connected` | Beim Übergang zu einer gültigen Statusverbindung. |
| `disconnected` | Bei Verbindungsverlust; `reason` enthält die Meldung. |
| `status` | Nach bestätigtem Status; `status` enthält alle Werte, `changed` nur Änderungen mit `old`/`new`, `first` kennzeichnet den ersten Status. |
| `alarm` | Wenn sich aktive Alarmidentitäten/Stufen ändern; `alerts`, `count`, `highest` (`none`, `warning`, `error`). |
| `command` | Ergebnis eines Lua-Auftrags; `source`, `ok`, `message`. Aus diesem Rückmeldeereignis darf kein neuer Auftrag gestartet werden. |

Jedes Ereignis enthält außerdem `timestamp`. `nil` in Lua bedeutet bei
`event.changed.<tag>.old` oder `.new`, dass das Feld vorher fehlte beziehungsweise
entfernt wurde.

## API

### `airctrl.set { ... }`

Fordert genau eine Schaltung aus einem `connected`-, `status`-, `alarm`- oder
`time`-Ereignis an. Pro Ereignis ist höchstens ein Auftrag erlaubt. Er durchläuft
dieselben Sperren und die Statusbestätigung wie ein manueller Klick.

| Feld | Lua-Typ | Erlaubte Werte |
|---|---|---|
| `pwr` | String | `"0"`, `"1"` |
| `cl` | Boolean | `false`, `true` |
| `mode` | String | `"P"`, `"A"`, `"S"`, `"M"` |
| `om` | String | `"1"`, `"2"`, `"3"`, `"s"`, `"t"` |
| `func` | String | `"P"`, `"PH"` |
| `uil` | String | `"0"`, `"1"` |
| `rhset` | Integer | `40`, `50`, `60`, `70` |
| `aqil` | Integer | `0`, `25`, `50`, `75`, `100` |
| `dt` | Integer | `0` bis `12` |

Integerfelder und String-/Boolean-Felder können wegen der Geräteprotokollkodierung nicht
im selben Auftrag gemischt werden. Zwei getrennte Zeitpläne müssen verschiedene
Uhrzeiten haben; für eng aufeinanderfolgende Aktionen mindestens eine Minute
Abstand verwenden.

### `airctrl.schedule { ... }`

Registriert beim Laden einen Zeitplan mit `name`, `at`, optional `days`, optional
`catch_up` und der Steuerwerttabelle `set`. `name` darf 1–64 Zeichen aus
`A–Z`, `a–z`, `0–9`, `_`, `.`, `-` enthalten und muss eindeutig sein. Pro
Skript sind höchstens 64 Zeitpläne erlaubt; `catch_up` ist ein Boolean.

### `airctrl.status()`

Liefert eine Kopie des zuletzt bestätigten Status als Lua-Tabelle. Vor dem ersten
Status ist die Tabelle leer. Das Gerät wird dafür nicht zusätzlich abgefragt.

Beim AC2729/10 deutet `status.dtrs` auf die verbleibenden Minuten des
Geräte-Abschalttimers hin: Im [Mitschnitt vom 2026-09-08](PROTOCOL_VALIDATION_2026-09-08.md)
folgten auf `dt=6` die Werte `dtrs=360` und etwa eine Minute später `359`.
`dt` ist dagegen die eingestellte Stundenzahl. Die Deutung von `dtrs` ist
beobachtet, nicht von Philips bestätigt; fehlende Werte bleiben `nil`.
`dtrs` darf nicht mit `airctrl.set` geschrieben werden. Dieser Gerätetimer ist
unabhängig von den lokalen Uhrzeitregeln in `airctrl.schedule`.

Lua-Schaltungen verwenden seit v1.06 wie die Tasten ausschließlich die TCP-
Verbindung zum zentralen `airctrl-server`. Lua öffnet niemals selbst UDP oder
eine Verbindung zum Gerät. Alle Clients teilen dieselbe Geräte-I/O-Sitzung.
Dabei wird Observe kurz ab- und wieder angemeldet; ein neuer Socket oder Sync
ist dafür nicht nötig. Erst ein gültiger Status bestätigt den neuen Zustand.
Ein Status-Timeout erneuert die Sitzung nach der Wiederverbindungspause;
dadurch wird ein bereits versuchter Zeitplanauftrag nicht erneut gesendet.

### `airctrl.log(level, message)`

Schreibt `debug`, `info`, `warning` oder `error` in das begrenzte Lua-Protokoll
der Diagnose. Die Stufe `error` ist nur ein Protokolleintrag; ein tatsächlicher
Skriptfehler entsteht durch ungültige Syntax, API-Nutzung oder Laufzeitfehler.

## Sandbox und Grenzen

Bereitgestellt werden Lua-Basisfunktionen sowie `table`, `string`, `math` und
`utf8`. Nicht vorhanden sind `io`, `os`, `package`, `debug`, `require`, `dofile`,
`loadfile` und `load`. Das Skript kann daher keine beliebigen Dateien lesen,
Programme starten, Netzwerkverbindungen öffnen oder Module nachladen. Nur
`airctrl.set` übergibt erlaubte Steuerwerte an den bereits konfigurierten
AirControl-Gerätepfad.

Der Lua-Zustand ist auf 8 MiB begrenzt; jeder Skript- oder Ereignisaufruf auf
200.000 VM-Instruktionen. Skripte dürfen höchstens 256 KiB groß sein. Bei einem
Fehler werden weitere Ereignisse und Zeitpläne bis zum erfolgreichen Neuladen
blockiert. Diese Begrenzungen sind eine zusätzliche Schutzschicht, keine
Garantie für fremden Code. Nur selbst geprüfte Skripte aktivieren.

Lua ist eine einbettbare Sprache; die Host-Anwendung bestimmt die angebotenen
C-Funktionen. Grundlage ist das [offizielle Lua-5.4-Handbuch](https://www.lua.org/manual/5.4/manual.html).
Quellherkunft und Prüfsumme stehen unter
[`third_party/lua/ORIGIN.md`](../third_party/lua/ORIGIN.md).
