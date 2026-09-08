# Zu AirCtrl-Desklet beitragen

Danke für dein Interesse! Kleine, nachvollziehbare Änderungen sind willkommen.
Deutsch und Englisch sind beide in Ordnung; die Oberfläche ist derzeit deutsch.

## Vor einer Änderung

- Suche in bestehenden Issues. Beschreibe bei größeren Änderungen zuerst das Ziel.
- Für Fehler nutze die Vorlage und nenne Version, Modell, Distribution, Qt-Version
  und das tatsächlich verwendete Qt-Backend (`xcb`/`wayland`).
- Entferne Gerätekennungen, IP-/MAC-Adressen, Benutzernamen, Pfade und andere private
  Angaben aus Berichten und Screenshots. Lade keine unbearbeiteten PCAP-Dateien hoch.
- Sicherheitsprobleme bitte **nicht** öffentlich melden: [SECURITY.md](SECURITY.md).

## Entwickeln und prüfen

Die vollständige Einrichtung steht in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
Kurzform mit CMake ab 3.21 und Ninja:

```bash
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev
python3 scripts/check_repository.py
python3 -m unittest discover -s tests -p 'test_repository.py' -v
```

Tests und Demo dürfen kein echtes Gerät ansprechen. Integrationstests binden nur
an Loopback und verwenden dynamische Ports. Geräteschreibbefehle sind niemals ein
Build-, Installations- oder CI-Schritt. Ein Hardwaretest ist eine separate,
bewusste Entscheidung der testenden Person.

## Qualitätsmaßstab

- Begrenze den Pull Request auf eine verständliche Aufgabe und ergänze einen
  Regressionstest für Fehlerkorrekturen.
- Halte UI-, Controller- und Protokollzustände auseinander. Ein Schreib-ACK ist
  kein neuer Gerätestatus. Wiederhole Schaltbefehle nicht automatisch.
- Lua-Regeln dürfen ausschließlich über die gemeinsame Steuerwert-Positivliste
  gehen. Neue Ereignisse brauchen deterministische Tests; Datei-, Prozess- oder
  Netzwerkzugriff gehört nicht in die Skript-Sandbox.
- Dokumentiere Modellgrenzen und Unsicherheiten. Unbekannte `err`-Bits werden
  nicht geraten; eine lokale Warnschwelle ist keine Herstellerangabe.
- Keine stillen Änderungen an Geräte-IP, gespeicherten Einstellungen oder
  Timeout-Verhalten. Rückwärtskompatibilität gehört in die Beschreibung.
- Neue Oberflächenzustände bitte mit synthetischen Daten abbilden. Keine privaten
  Screenshots oder Netzwerkaufzeichnungen einchecken.
- Erkenntnisse aus privaten Mitschnitten nur anonymisiert mit Paketart, relativer
  Zeit, Zählerverlauf und Interpretationsgrenzen dokumentieren. Keine PCAP-/LZ4-
  Datei oder Gerätekennung in Repository, Release-Anhang oder Testfixture übernehmen.
- Nutze C++17, bestehende Qt-Konventionen und vier Leerzeichen. Formatiere keine
  unbeteiligten Dateien neu. Keine neuen Abhängigkeiten ohne Begründung.
- Prüfe Warnungen und dokumentiere übersprungene Tests sowie nicht getestete
  Desktop-/Hardwarekombinationen ehrlich.

## Pull Request und Lizenz

Erkläre Problem, Lösung, Testnachweis und verbleibende Grenzen. Nenne substanzielle
KI-Unterstützung transparent; du bleibst für das eingereichte Ergebnis
verantwortlich. Lade keine fremden Geheimnisse oder nicht freigegebenen Inhalte
in externe Hilfsdienste.

Mit einem Beitrag bestätigst du, dass du ihn unter der [MIT-Lizenz](LICENSE)
dieses Projekts bereitstellen darfst. Urheber- und Herkunftshinweise bleiben
erhalten. Es gibt keine Zusage zu Bearbeitungszeit oder Unterstützung bestimmter
Geräte. Bitte beachte unseren [Verhaltenskodex](CODE_OF_CONDUCT.md).
