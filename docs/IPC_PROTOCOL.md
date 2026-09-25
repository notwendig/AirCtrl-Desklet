# AirControl-TCP-Protokoll

Seit v1.06 kommuniziert ausschließlich `airctrl-server` mit dem Philips AC2729.
Desklet, Lua-Skripteditor und `airctrl-client` kennen nur den TCP-Endpunkt des
Servers, standardmäßig `localhost:5680`. Gerätehostname, UDP-Port und Gerätefristen
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

Der Server sendet beim Verbindungsaufbau seinen Geräte- und Lua-Zustand und bei
aktiver Geräteverbindung zusätzlich den letzten Status. Gerätestatus und
Zustandswechsel gehen an alle Clients. Ein Schaltergebnis geht nur an den
Auftraggeber. Der Lua-Skripttext geht ausschließlich an den Client, dem die
serverweite Editier-Sperre erteilt wurde.

## Clientnachrichten

| `_airctrl` | Felder | Bedeutung |
|---|---|---|
| `control` | `id`, `values` | Einen positiv geprüften Geräteauftrag genau einmal versuchen |
| `refresh` | – | Geräte-I/O im Server schließen, neu öffnen und synchronisieren |
| `ping` | – | IPC-Verbindung ohne Gerätezugriff prüfen |
| `automation_edit_begin` | `id` | Exklusive Editier-Sperre anfordern und aktuelle Serverfassung laden |
| `automation_edit_save` | `id`, `revision`, `enabled`, `script` | Gesperrte Fassung serverseitig prüfen, speichern und neu laden |
| `automation_edit_cancel` | – | Eigene Editier-Sperre ohne Änderung freigeben |
| `automation_resume` | – | Manuelle Automatik-Sperre aufheben und Lua sofort neu auswerten |

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
| `automation_state` | `enabled`, `loaded`, `manual_override`, `revision`, `schedule_count`, Diagnosefelder, `editor_busy` | Serverweiter Lua-Zustand ohne Skripttext |
| `automation_edit` | `id`, `ok`, bei Erfolg `script`, `revision`, `state` | Erteilte Sperre und Serverfassung oder Ablehnungsgrund |
| `automation_saved` | `id`, `ok`, bei Erfolg `state`, sonst `error` | Ergebnis der serverseitigen Prüfung und Speicherung |
| `automation_edit_released` | `ok` | Bestätigung der Sperrfreigabe |

Gleichzeitig besitzt höchstens eine TCP-Verbindung die Editier-Sperre. Ein
zweiter Client erhält `automation_edit` mit `ok=false`. Bei Abbruch der
Eigentümerverbindung gibt der Server die Sperre ohne Zeitverzug frei. Eine
fehlgeschlagene Skriptprüfung lässt die Sperre beim Eigentümer, damit der Fehler
im Editor korrigiert werden kann. Erfolgreiches Speichern oder Abbrechen gibt sie
frei. `revision` verhindert das Überschreiben einer nicht mehr aktuellen Fassung.

Das Desklet sendet im Leerlauf regelmäßig `ping`. Bleibt `pong` aus, verwirft es
die scheinbar noch bestehende TCP-Verbindung und verbindet sich selbstständig
neu. Dadurch wird ein stiller Netz- oder Serverausfall ohne Schaltbefehl erkannt.

Ein gültiger manueller `control`-Auftrag mit `pwr`, `mode`, `om`, `func`,
`rhset` oder `dt` setzt `manual_override=true` und hält Lua auf allen Clients
sichtbar an. Reine Licht-/Anzeigeaufträge (`aqil`, `uil`) und die
Kindersicherung (`cl`) tun dies nicht. `automation_resume` hebt die Sperre auf;
der Server wertet danach den letzten Status und den aktuell gültigen Zeitplan
neu aus. Die Nachricht selbst erzeugt keinen Power-Auftrag.

Ein `ok=true` bestätigt zunächst die Annahme durch das Gerät. Desklet und die
serverseitige Lua-Automatik warten weiterhin auf die nächste Statusmeldung, bevor sie die Zustandsänderung
als bestätigt anzeigen. Schaltaufträge werden nach Timeout oder Verbindungsfehler
nicht automatisch wiederholt. Wird die Geräte-I/O während eines bereits laufenden
Auftrags erneuert, meldet der Server dessen Ausgang ausdrücklich als unbekannt;
ein Status aus der neuen Sitzung darf ihn nicht fälschlich bestätigen.
Mehrere Clientaufträge werden serverweit nacheinander verarbeitet. Zwischen zwei
Versuchen muss mindestens eine neue Statusmeldung des Geräts eingegangen sein.

## Geräte-I/O

Der Server verarbeitet Observe und Control nacheinander auf einem einzigen
UDP-Socket. Ein Schaltauftrag meldet Observe kurz ab, sendet Control und meldet
Observe auf demselben Socket wieder an. Der Server bindet `device/local_port`
und hält Firewall-/NAT-Zustände während ereignisbedingter Sendepausen mit einem
leeren CoAP-CON im Abstand `device/keepalive_ms` offen. Nach
`device/idle_ms` ohne Status registriert er Observe zunächst bis zu
`device/observe_refreshes` Mal mit demselben Token und derselben UDP-Sitzung
neu. Erst wenn auch die jeweils höchstens `device/request_ms` lange Antwortfrist
abläuft, meldet er Observe ab und empfängt noch bis zu
`device/cancel_grace_ms` lang auslaufende Pakete. Danach wird der Geräteclient
zerstört. Nach `device/reconnect_ms` öffnet der Server einen neuen UDP-Socket
und synchronisiert `/sys/dev/sync` erneut. Die TCP-Verbindungen der Clients
bleiben dabei bestehen.
