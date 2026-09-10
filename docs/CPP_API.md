# C++-API-Dokumentation

Die eigenen C++-Schnittstellen in `src/*.hpp` sind auf Englisch im Doxygen-
Stil dokumentiert. Die Kommentare beschreiben Zuständigkeit, Parameter,
Rückgabewerte und wichtige Zustands- oder Sicherheitsgrenzen. Insbesondere ist
festgehalten, dass `Controller`, `Desklet` und `AutomationEngine` ausschließlich
Clients des TCP-Servers sind und keine direkte Geräteverbindung öffnen.

Die HTML-Dokumentation wird optional erzeugt; sie ist keine Build-Abhängigkeit:

```bash
sudo dnf install -y doxygen
doxygen Doxyfile
xdg-open build/doxygen/html/index.html
```

Doxygen läuft mit englischer Ausgabe und behandelt fehlerhafte Dokumentations-
Syntax als Fehler. Generierte HTML-Dateien gehören nach `build/doxygen` und
werden weder eingecheckt noch in das Quell-ZIP aufgenommen.

Die Implementierungsdateien enthalten zusätzliche englische Kommentare nur an
nicht offensichtlichen Stellen: Threadgrenzen, Befehlsserialisierung,
Statusbestätigung, Session-Neuaufbau und Testisolierung. Kommentare sollen das
Warum und die garantierten Grenzen erklären, nicht den C++-Code nacherzählen.
