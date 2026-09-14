# Protokoll- und Firewallvalidierung am AC2729/10 – 2026-09-13

Diese Ergänzung wertet zwei private Ethernet-Mitschnitte des zentralen
`airctrl-server` aus. Rohmitschnitte, lokale Adressen und Gerätekennungen bleiben
außerhalb des öffentlichen Quellpakets.

## Ergebnis

Der AC2729 blieb während der gemeldeten Fehler erreichbar. Ursache der
Wiederverbindungsschleife war das Zusammenspiel aus ereignisgesteuertem CoAP
Observe, dynamischen lokalen UDP-Ports und der Host-Firewall des Servers.

Im zweiten Mitschnitt treffen drei formal gültige `2.05 Content`-
Statusmeldungen 35 bis 55 Sekunden nach der Observe-Anmeldung ein. Der Serverhost
beantwortet sie innerhalb weniger Zehntel Millisekunden mit ICMP Typ 3, Code 13
(`communication administratively prohibited`). Die Anwendung erhält diese
Pakete deshalb nicht und meldet nach 60 Sekunden `CoAP response timed out`.
Synchronisationsantworten desselben Geräts benötigen dagegen nur 6 bis 15 ms.

| Observe-Versuch | Erste/weitere Statusmeldung | Ergebnis am Server |
|---:|---:|---|
| 1 | keine innerhalb 60 s | lokaler Antwort-Timeout |
| 2 | 55,141 s | gültiges Statuspaket, sofort per ICMP Code 13 abgewiesen |
| 3 | 35,236 s und 50,272 s | beide gültigen Pakete per ICMP Code 13 abgewiesen |
| 4 | 25,317 s | angenommen; danach stabiler Statusstrom |

Beim vierten Versuch kommt die erste Statusmeldung bereits nach 25 Sekunden und
wird angenommen. Danach bleibt derselbe Socket stabil; elf Statusmeldungen
werden entschlüsselt und innerhalb von Millisekunden an den TCP-Client
weitergegeben. Die längste Sendepause dieser Folge beträgt 77 Sekunden.

Der erste Mitschnitt zeigt zusätzlich den früheren 90-Sekunden-Grenzfall: Der
Server meldet Observe nach exakt 90 Sekunden ab und schließt den Socket. Nur
104 ms später sendet das Gerät noch eine gültige Meldung mit fortlaufender
Observe-Nummer an den alten Port; der Host antwortet nun mit ICMP Typ 3, Code 3
(`port unreachable`). Nach zehn Sekunden werden Socket und Synchronisierung neu
aufgebaut.

Alle entschlüsselten Statusobjekte enthalten `err=0`; das WLAN-Signal liegt
konstant bei etwa −35 dBm. Die CoAP-Observe-Folgen sind bis auf durch die
Aufzeichnung selbst nicht belegbare Zwischenpakete monoton. Es gibt keinen
Hinweis auf einen Geräte-Neustart, einen Kryptografiefehler oder einen Abbruch
der TCP-Verbindung zum Desklet.

## Abgeleitete Härtung

- fester, konfigurierbarer lokaler UDP-Port für eine enge Firewallregel;
- leeres CoAP-CON alle 20 Sekunden, unabhängig von Statusänderungen;
- 120 Sekunden bis zur ersten Observe-Antwort;
- nach einer Statusfrist genau eine Observe-Neuanmeldung mit gleichem Token und
  Socket, bevor die gesamte I/O-Sitzung erneuert wird;
- 300 ms Empfangsnachlauf nach der endgültigen Observe-Abmeldung;
- erneute Existenzprüfung eines TCP-Clients nach Ausgaben, die ihn wegen einer
  übergroßen Warteschlange trennen können;
- Einlesen der Lua-Beispieldatei bei jedem frischen CMake-Client-Build statt
  Abhängigkeit von einem alten Cachewert;
- unveränderte Regel: Schaltbefehle werden nach unklarem Ausgang niemals
  automatisch wiederholt.

Damit bleibt die Datenalteranzeige weiterhin ehrlich, ohne eine stille, aber
erreichbare Gerätesitzung vorschnell als getrennt zu behandeln.
