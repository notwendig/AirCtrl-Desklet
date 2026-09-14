# Abgleich der Philips-Protokollreferenzen – 2026-09-12

[Projektübersicht](../README.md) ·
[Architektur](ARCHITECTURE.md) ·
[reale AC2729/10-Validierung](PROTOCOL_VALIDATION_2026-09-08.md)

Für den Philips AC2729/10 gibt es keine veröffentlichte Herstellerspezifikation.
Dieser Abgleich verwendet deshalb drei unabhängige Reverse-Engineering-Quellen:

- [py-air-control](https://github.com/rgerganov/py-air-control) als ursprüngliche
  Referenz für Philips-HTTP-, Plain-CoAP- und verschlüsselte CoAP-Geräte;
- [aioairctrl 0.3.1](https://github.com/kongo09/aioairctrl/tree/cc280461366437be25b05a00952f647ecd974aff)
  als aktuelle verschlüsselte CoAP-Referenz vom 2026-03-01;
- [Philips AirPurifier CoAP](https://github.com/kongo09/philips-airpurifier-coap)
  für Modellfelder, bekannte Geräteinstabilität und Wiederverbindungsverhalten.

Das geprüfte PyPI-Quellarchiv `aioairctrl-0.3.1.tar.gz` hat SHA-256
`d74feb65d62d9151dc84144cf429fbd6c71994998b093472ec72a0cc1495b57a`.

## Übereinstimmungen

AirCtrl verwendet wie die Referenzen UDP-Port 5683, NON-Anfragen und die drei
Pfade `/sys/dev/sync`, `/sys/dev/status` und `/sys/dev/control`. Die Sync-Antwort
initialisiert einen achtstelligen Hex-Zähler. Vor einem Control wird er erhöht.
Der Nachrichtenkörper besteht aus Zähler, hexadezimalem AES-128-CBC-Chiffrat und
SHA-256-Prüfsumme. Schlüssel und IV sind die beiden ASCII-Hälften von
`MD5("JiangPan" + Zähler)`; PKCS#7-Padding und Großschreibung entsprechen der
Referenz.

Der Control-Inhalt besitzt weiterhin `state.desired` sowie `CommandType`,
`DeviceId` und `EnduserId`. Statusdaten werden ausschließlich aus
`state.reported` übernommen. Der öffentliche echte AC2729/10-Chiffretext aus
aioairctrl 0.3.1 wird nun direkt vom C++-Code entschlüsselt und auf Modell,
Power, Funktion und Feuchte geprüft.

## Übernommene Verbesserungen

- Eine optionale Wiederholungsfolge synchronisiert nur nach der ersten
  Ablehnung neu, nicht nach jeder weiteren und nicht unnötig nach dem letzten
  Versuch. Der Produktionsserver bleibt für Gerätebefehle bewusst bei null
  Wiederholungen und null Resync, damit ein unklar ausgegangener Befehl niemals
  automatisch erneut gesendet wird.
- Gültige Sammelbefehle dürfen JSON-Strings, Boolean- und Integerwerte gemeinsam
  enthalten. Die alte Einschränkung stammte aus der früheren globalen CLI-
  Option `-I`, nicht aus dem Philips-Nachrichtenformat.
- Der öffentliche AC2729/10-Testvektor und eine manipulierte Variante sichern
  Verschlüsselung und Digestprüfung unabhängig von einem bloßen Roundtrip ab.

## Zusätzliche lokale Härtung

- Ein Status ist nur gültig, wenn `state.reported` ein nichtleeres JSON-Objekt
  ist. Arrays, `null`, fehlende Ebenen und leere Objekte beenden die fehlerhafte
  Sitzung, statt den Server fälschlich schaltbereit zu machen.
- Ein piggybacked CoAP-ACK muss neben dem Token auch die Message-ID der Anfrage
  besitzen. Separate CON- oder NON-Antworten dürfen weiterhin eine eigene
  Message-ID verwenden.
- Leere CON-Nachrichten werden als CoAP-Ping mit Reset beantwortet. Fremde
  confirmable Antworten erhalten ebenfalls ein Reset.
- Der 32-Bit-Sendezähler wird bei `0xFFFFFFFF` nicht wiederverwendet. AirCtrl
  verlangt dann eine neue Synchronisierung; aioairctrl 0.3.1 lässt ihn
  rechnerisch auf null umlaufen.

## Bewusst nicht übernommen

- SSDP-Gerätesuche, Cloudzugriff, HTTP und unverschlüsseltes CoAP liegen
  außerhalb des AC2729/10-Serverziels.
- CoAP-Blockübertragung wird abgelehnt. Die erfassten AC2729-Nachrichten passen
  vollständig in ein UDP-Datagramm.
- `Max-Age` ersetzt nicht den konfigurierten 90-s-Observe-Wächter. Auch die
  aktuelle Referenz reicht `Max-Age` nur bei der einmaligen Statusabfrage zurück;
  die reale AC2729-Prüfung belegt den gewählten kontrollierten Neuaufbau nach
  einer längeren Sendepause.
- Neuere Beispiele anderer Philips-Modelle verwenden teils Boolean- oder
  Integerwerte für Power und Lüfterstufe. AirCtrl behält die am AC2729/10
  bestätigten Typen `pwr="0"|"1"` und `om="1"|"2"|"3"|"s"|"t"` bei.

## Regressionstests

`protocol-tests` läuft unabhängig von Qt im Server- oder Client-Testbuild und
prüft den echten AC2729/10-Chiffretext, Digestmanipulation, falsche ACK-Message-
IDs, ungültige Statusstrukturen und genau eine Resynchronisierung bei mehreren
abgelehnten Testbefehlen. Die bestehende Desklet-Suite prüft zusätzlich einen
gemischten Sammelbefehl durch TCP-Server, echten C++-CoAP-Transport und lokalen
UDP-Gerätesimulator.
