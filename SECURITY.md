# Sicherheit / Security

## Unterstützter Stand

Sicherheitskorrekturen sind für den aktuellen `main`-Stand und die jeweils neueste
1.x-Veröffentlichung vorgesehen. Das ist ein gemeinschaftlich gepflegtes Projekt,
kein Produkt mit garantierter Reaktionszeit. Alte ZIP-Versionen erhalten keine
zugesicherte Rückportierung. Der aktuelle vorbereitete Anwendungsstand ist 1.02.

## Vertraulich melden

Bitte veröffentliche **keine ausnutzbaren Sicherheitslücken**, Zugangsdaten,
vollständigen Diagnoseberichte oder Rohmitschnitte als normales Issue.

Wenn im GitHub-Repository **Security → Advisories → Report a vulnerability**
angeboten wird, nutze diesen privaten Meldeweg. Er muss vom Maintainer nach dem
Anlegen des Repositorys aktiviert werden; diese Datei aktiviert ihn nicht.
Fehlt die Funktion, frage in einem Issue ohne technische Details nach einem
privaten Kontaktweg und warte auf die Antwort. Es wird hier bewusst keine
unbestätigte E-Mail-Adresse genannt.

Hilfreich sind: betroffener Commit/Version, Modell, Voraussetzungen,
minimaler Reproduktionsfall gegen einen Simulator, erwartete Auswirkungen und
gegebenenfalls ein Korrekturvorschlag. Keine Versuche an fremden Geräten.

## Grenzen und Betrieb

- Das Widget kommuniziert mit einem lokalen Gerät über UDP/CoAP (standardmäßig
  Port 5683). Stelle diesen Port nicht durch Routerfreigaben ins Internet.
- Die implementierte Philips-Protokollverschlüsselung ist kein Nachweis einer
  modernen, gegenseitig authentifizierten Vertrauensbeziehung. Betreibe Gerät und
  Client in einem kontrollierten lokalen Netz.
- Diagnoseberichte können Gerätekennungen, Hostadressen, lokale Programmpfade,
  Status und Nutzungsinformationen enthalten. Die Kopierfunktion anonymisiert
  sie **nicht automatisch**. Vor einer Veröffentlichung manuell prüfen.
- `--demo` und die automatisierten Tests benötigen kein echtes Gerät. Verwende
  sie für reproduzierbare öffentliche Fehlerberichte.
- `install.sh` baut und installiert Software im Benutzerpräfix; dafür ist kein
  `sudo` nötig. Lies fremde Pull Requests und Skripte vor der Ausführung.
- Die vorbereitete CI enthält keine Gerätezugänge und verwendet keine
  Produktionsgeheimnisse. Actions sind auf vollständige Commit-SHAs festgelegt.
  Release-Schreibrechte sind auf den Release-Job beschränkt.
- Die Lua-Automatik ist standardmäßig aus. Ihre Sandbox öffnet keine Datei-,
  Betriebssystem-, Paket- oder Debug-Bibliothek und begrenzt Speicher sowie
  Instruktionen. Sie ist dennoch keine Garantie für unvertrauenswürdige Skripte:
  nur selbst geprüften Code aktivieren. Lua-Aufträge können das reale Gerät
  schalten und gehören deshalb wie manuelle Befehle in ein kontrolliertes Netz.

Filter- und Datenalteralarme sind Komfortfunktionen, keine sicherheitskritische
Überwachung. Herstellerhinweise und die Anzeige des tatsächlichen Geräts haben
bei Wartungsentscheidungen Vorrang.
