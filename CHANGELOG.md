# Änderungsübersicht

## v1.00 Stable – 2026-09-02

Erste stabile Ausgabe, unveränderter Funktionsstand von 0.3.8.

- Versionskennung in CMake, Programm, Diagnose und Dokumentation auf 1.00 gesetzt.
- Vollständiges Quellpaket mit Installer, Backend, Tests und Vorschauen.
- Bestehende Einstellungen, Autostart und Installationspfade bleiben erhalten.
- Keine Änderung an CoAP-Kommunikation, Schaltbefehlen, Oberfläche oder Alarmverhalten.

Enthalten sind die acht Gerätetasten, aktive Status-Embleme, runde Datenalter- und
Alarmanzeigen, anpassbare Darstellung, der gespeicherte Haken für die
Fensterdekoration sowie die Diagnose mit Hexcodes und unveränderten Rohdaten.

Die Bezeichnung Stable ist keine zusätzliche Hardware- oder Desktop-Zertifizierung.
Der Umfang der automatisierten Prüfungen und die weiterhin nicht in einer echten
Cinnamon-Sitzung geprüften Funktionen stehen in VALIDATION.md.

## 0.3.8

- Fensterdekoration über einen gespeicherten Kontextmenü-Haken umschaltbar.
- Fehler-/Statuscodes zusätzlich als Hexwerte in Diagnose und Kopierbericht.
- Datenalter und Alarm als Kreise neben den Emblemen im Statusfeld.

## 0.3.7

- Sekundenzähler seit dem letzten gültigen Statuspaket mit konfigurierbarer Ampel.
- Sichtbare Warnungen/Fehler, Alarmdetails und Quittierung.
- Desktop-Benachrichtigungen und optionaler Signalton.

## 0.3.6

- Aktive Display-Embleme aus dem bestätigten Gerätestatus.

## 0.3.5

- Dauerhafte CoAP-Beobachtung statt zyklischer Einzelabfragen.
- Getrennte Schreibprozesse, bestätigte Schaltzustände und Elternprozessschutz.
