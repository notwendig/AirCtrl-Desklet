# AirControl-TCP-Protokoll

Seit v1.06 kommuniziert ausschließlich `airctrl-server` mit dem Philips AC2729.
Desklet, Lua-Automatik und `airctrl-client` kennen nur den TCP-Endpunkt des
Servers, standardmäßig `nadhh:5680`. Gerätehostname, UDP-Port und Gerätefristen
stehen ausschließlich in `/etc/airctrld.cfg`.

## Endpunkt und Sicherheit

- Transport: TCP
- Standardport: 5680
- Standard-Listenadresse: `0.0.0.0`
- Rahmen: UTF-8-JSON, genau ein Objekt pro Zeile
- Höchstgröße: 1 MiB je Clientpuffer

Das Protokoll besitzt keine Anmeldung und keine Transportverschlüsselung. Port
5680 darf deshalb nur für vertrauenswürdige Rechner im lokalen Netz erreichbar
sein und niemals aus dem Internet veröffentlicht werden.

Der Server sendet beim Verbindungsaufbau seinen Zustand und bei aktiver
Geräteverbindung zusätzlich den letzten Status. Gerätestatus und Zustandswechsel
gehen an alle Clients. Ein Schaltergebnis geht nur an den Auftraggeber.

## Clientnachrichten

| `_airctrl` | Felder | Bedeutung |
|---|---|---|
| `control` | `id`, `values` | Einen positiv geprüften Geräteauftrag genau einmal versuchen |
| `refresh` | – | Geräte-I/O im Server schließen, neu öffnen und synchronisieren |
| `ping` | – | IPC-Verbindung ohne Gerätezugriff prüfen |

Eine `configure`-Nachricht wird abgewiesen. Clients dürfen die Gerätekonfiguration
nicht ändern.

Beispiel:

```json
{"_airctrl":"control","id":17,"values":{"mode":"S","om":"s","uil":"0"}}
```

## Servernachrichten

| `_airctrl` | Felder | Bedeutung |
|---|---|---|
| `state` | `state`, `starts`, optional `error` | `starting`, `connecting`, `connected` oder `error`; `starts` zählt serverweite Geräte-I/O-Starts |
| `configured` | `host`, `port` | Konfiguration angenommen |
| `status` | `data` | Vollständiger entschlüsselter Gerätestatus |
| `control` | `id`, `ok`, optional `error` | Ergebnis des gleich bezeichneten Clientauftrags |
| `pong` | – | Antwort auf `ping` |
| `error` | `error` | Ungültige IPC-Nachricht oder Anfrage |

Ein `ok=true` bestätigt zunächst die Annahme durch das Gerät. Desklet und Lua
warten weiterhin auf die nächste Statusmeldung, bevor sie die Zustandsänderung
als bestätigt anzeigen. Schaltaufträge werden nach Timeout oder Verbindungsfehler
nicht automatisch wiederholt. Wird die Geräte-I/O während eines bereits laufenden
Auftrags erneuert, meldet der Server dessen Ausgang ausdrücklich als unbekannt;
ein Status aus der neuen Sitzung darf ihn nicht fälschlich bestätigen.
Mehrere Clientaufträge werden serverweit nacheinander verarbeitet. Zwischen zwei
Versuchen muss mindestens eine neue Statusmeldung des Geräts eingegangen sein.

## Geräte-I/O

Der Server verarbeitet Observe und Control nacheinander auf einem einzigen
UDP-Socket. Ein Schaltauftrag meldet Observe kurz ab, sendet Control und meldet
Observe auf demselben Socket wieder an. Nach `device/idle_ms` ohne Status wird
der Geräteclient zerstört. Nach `device/reconnect_ms` öffnet der Server einen
neuen UDP-Socket und synchronisiert `/sys/dev/sync` erneut. Die TCP-Verbindungen
der Clients bleiben dabei bestehen.
