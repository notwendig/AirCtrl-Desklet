# Protokollvalidierung am AC2729/10 – 2026-09-08

[Projektübersicht](../README.md) · [Architektur](ARCHITECTURE.md) ·
[Gesamter Prüfstand](../VALIDATION.md)

Diese Notiz dokumentiert die reale Geräteprüfung der v1.04-I/O-Sitzung. Zwei
Ethernet-Aufzeichnungen wurden offline ausgewertet und auf CoAP/UDP-Port 5683
zwischen genau einem Client und dem Philips AC2729/10 begrenzt. Paketinhalt,
Hash und Verschlüsselungsformat wurden geprüft; Status- und Control-Nutzdaten
wurden zur Zuordnung lokal entschlüsselt.

Die Rohmitschnitte, lokale IP-Adressen, Gerätekennungen und der Gerätename werden
bewusst nicht veröffentlicht. `*.pcap`, `*.pcapng`, komprimierte Varianten und
`*.lz4` sind ignoriert und nicht Bestandteil von Repository oder Quell-ZIP.
Die Paketnummern dienen ausschließlich zur Zuordnung im privaten Original.

## Umfang und Qualität

| Aufzeichnung | Zeitraum (MESZ) | Dauer | Relevante CoAP-Pakete | Status | Control |
|---|---:|---:|---:|---:|---:|
| A | 00:19:33,125–00:48:02,939 | 1.709,814 s | 115 | 111 | 0 |
| B | 00:56:56,393–01:03:26,523 | 390,130 s | 105 | 37 | 17 |
| Summe | zwei getrennte Bereiche | 2.099,944 s | 220 | 148 | 17 |

Die Bereiche überlappen sich nicht; dazwischen fehlen 564,744 s. Die
PCAPNG-Statistik meldet keine verworfenen Pakete, kein relevantes UDP-Paket ist
gekürzt oder fragmentiert. Im gefilterten Verkehr treten keine ICMP-Fehler,
CoAP-Fehler, Digestfehler oder Decoderfehler auf. Alle 148 Statusmeldungen
enthalten `err=0`.

Innerhalb jeder beobachteten Folge erhöhen sich sowohl die CoAP-Observe-Nummer
als auch der Nachrichtenpräfix genau um eins. Dass im nicht aufgezeichneten
Zwischenraum Werte fehlen, wird nicht als Paketverlust der Aufzeichnung gewertet.

## Erneuerung nach Status-Timeout

| Ereignis | Paket | Zeitpunkt (MESZ) | Abstand |
|---|---:|---:|---:|
| Letzter Status auf altem Clientport `34482` | 60586 | 00:33:51,655 | – |
| Observe-Abmeldung auf altem Port | 72891 | 00:35:21,655 | 89,9999 s nach Status |
| `/sys/dev/sync` auf neuem Clientport `38577` | 73793 | 00:35:31,339 | 9,6842 s nach Abmeldung |
| Observe-Anmeldung auf neuem Port | 73795 | 00:35:31,343 | 0,004 s nach Sync-Antwort |
| Erster gültiger Status der neuen Sitzung | 76352 | 00:36:07,772 | 36,4292 s nach Anmeldung |

Zwischen dem letzten alten und dem ersten neuen Status liegen **136,1168 s**.
Das entspricht der Implementierung: 90-s-Statusfrist, konfigurierte
Wiederverbindungspause von ungefähr 10 s und anschließend die vom Gerät
bestimmte Zeit bis zur ersten Observe-Antwort.

Der Netzwerkmitschnitt zeigt keine Systemaufrufe. Der neue Quellport zusammen
mit dem unmittelbar folgenden `/sys/dev/sync` belegt jedoch den Neuaufbau auf
Protokollebene. Der vorige Port verschwindet danach vollständig.

## Gemeinsamer Socket beim Schalten

Alle folgenden Schritte verwenden Clientport `38577`. Vor jedem Control wird
die bisherige Observe-Anfrage mit demselben Token und Observe-Wert 1 abgemeldet.
Danach folgen Control und eine neue Observe-Anmeldung mit frischem Token.
Observe-Wert 0 bedeutet Anmeldung, 1 bedeutet Abmeldung gemäß RFC 7641.
Dies ist eine neue Beobachtung auf demselben Socket, kein Socket- oder
Session-Neustart.

| Nr. | Zielwerte | Control-Paket | `success` nach | Bestätigt nach |
|---:|---|---:|---:|---:|
| 1 | `cl=true` | 1346 | 7,3 ms | 83,4 ms |
| 2 | `cl=false` | 2992 | 5,8 ms | 91,6 ms |
| 3 | `pwr="0"` | 4407 | 5,7 ms | 50,2 ms |
| 4 | `pwr="1"` | 15914 | 8,5 ms | 69,8 ms |
| 5 | `mode="M", om="1"` | 16619 | 7,2 ms | 91,3 ms |
| 6 | `mode="M", om="2"` | 16791 | 6,0 ms | 44,5 ms |
| 7 | `mode="M", om="3"` | 16967 | 6,0 ms | 51,9 ms |
| 8 | `mode="M", om="t"` | 17167 | 6,0 ms | 91,2 ms |
| 9 | `mode="P"` | 17407 | 8,0 ms | 89,0 ms |
| 10 | `mode="A"` | 17542 | 6,0 ms | 87,2 ms |
| 11 | `mode="S", om="s"` | 17764 | 6,9 ms | 83,4 ms |
| 12 | `mode="P"` | 17978 | 7,0 ms | 54,9 ms |
| 13 | `aqil=50` | 20599 | 7,1 ms | 89,1 ms |
| 14 | `uil="0"` | 22314 | 7,0 ms | 87,3 ms |
| 15 | `aqil=100` | 25772 | 7,0 ms | 88,2 ms |
| 16 | `func="P"` | 31004 | 13,4 ms | 97,0 ms |
| 17 | `dt=6` | 32633 | 6,0 ms | 89,5 ms |

Jede `success`-Antwort gehört über ihren CoAP-Token eindeutig zum Befehl. Der
unmittelbar nächste entschlüsselte Gerätestatus enthält in allen 17 Fällen die
gewünschten Werte. Annahmezeiten liegen zwischen 5,7 und 13,4 ms, bestätigte
Zustände zwischen 44,5 und 97,0 ms nach dem Control-Paket.

Im gesamten Schaltabschnitt erscheint kein `/sys/dev/sync`. Der aus dem ersten
Sync abgeleitete Control-Zähler läuft lückenlos von `0x36A2D909` bis
`0x36A2D919`. Damit sind derselbe Socket und die fortgesetzte Synchronisierung
für sämtliche 17 Schaltvorgänge bestätigt.

Nach dem Ausschalten entsteht zwischen Paket 4880 und 15921 eine Statuspause
von 65,774 s. Sie bleibt unter 90 s und führt weder zu einem Portwechsel noch
zu einem Sync. Erst danach wird das Gerät wieder eingeschaltet.

## Bedeutung von Synchronisierung und Nachrichtenpräfix

`/sys/dev/sync` liefert den Ausgangswert für die Sendeseite. Vor einem
verschlüsselten Control wird er erhöht. Der achtstellige Hex-Präfix jeder
Nachricht wird zusammen mit der festen Zeichenfolge `JiangPan` gehasht. Die
ersten 16 ASCII-Zeichen des großgeschriebenen MD5-Ergebnisses bilden den
AES-128-CBC-Schlüssel, die zweiten 16 den IV. Eine SHA-256-Prüfsumme schützt den
übertragenen Nachrichtenkörper.

Der Ausdruck „Session-Key“ ist daher nur eine vereinfachende Bezeichnung für
den synchronisierten Protokollzustand. Es gibt keinen für alle Pakete konstanten
AES-Schlüssel. Eingehende Statusmeldungen enthalten ihren jeweiligen Präfix und
können daraus unabhängig entschlüsselt werden.

## Beobachteter Gerätetimer

Der letzte Befehl setzt `dt=6`. Der erste bestätigende Status enthält
`dt=6, dtrs=360`; weitere Statusmeldungen zeigen zunächst weiterhin 360 und
etwa eine Minute später 359. Das ist starke empirische Evidenz dafür, dass:

- `dt` die eingestellte Laufzeit in Stunden enthält;
- `dtrs` die verbleibende Laufzeit in Minuten enthält.

Diese Zuordnung ist nicht als Philips-Spezifikation belegt. AirCtrl-Desklet
zeigt und erklärt den Rohwert, schreibt `dtrs` nicht und leitet daraus keinen
Alarm ab. Lua darf ihn über `event.status.dtrs` beziehungsweise
`airctrl.status().dtrs` lesen, aber nicht mit `airctrl.set` setzen.

## Grenzen und Schlussfolgerung

Die Ursache der mindestens 90 Sekunden langen Sendepause ist aus den Paketen
nicht bestimmbar. CoAP Observe verlangt bei unverändertem Zustand keine
periodische Nachricht; deshalb kann die Clientseite ohne eigene Aktualisierung
nicht zwischen Ruhe und Verbindungsverlust unterscheiden. Die lokale
90-s-Frist bleibt eine bewusst gewählte Produktregel.

Bestätigt sind:

- ein UDP-Socket für Sync, Status und alle Schaltungen einer gesunden Sitzung;
- keine Neusynchronisierung beim Schalten;
- genau eine Ab- und Neuanmeldung von Observe je Schaltvorgang;
- keine automatische Wiederholung der 17 Control-Pakete;
- Close/Open mit neuem Quellport und neuem Sync erst nach Status-Timeout;
- jeder angenommene Schaltwert durch den nächsten Status bestätigt.

Nicht bestätigt sind die Ursache der Sendepause, der nicht aufgezeichnete
Zwischenzeitraum sowie sämtliche nicht getesteten Geräte- und Netzwerkfehler.
