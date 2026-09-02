#include "desklet.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("AirControl");
    QCoreApplication::setApplicationName("airctrl-desklet");
    QCoreApplication::setApplicationVersion("0.3.7");
    QApplication::setApplicationDisplayName("Philips AirControl");
    QApplication::setStyle("Fusion");
    QCommandLineParser parser;
    parser.setApplicationDescription("Qt6-Desktopwidget für Philips AC2729/10");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOptions({
        {{"H","host"}, "Geräteadresse (Standard: 192.168.77.5)", "host"},
        {{"P","port"}, "UDP-Port (Standard: 5683)", "port"},
        {"window", "Als normales Fenster starten"},
        {"demo", "Vorschau ohne Geräteverbindung oder Speichern von Einstellungen"},
        {"screenshot", "Vorschau als PNG speichern und beenden", "file"},
        {"reset-position", "Gespeicherte Position für diesen Start zurücksetzen"},
    });
    parser.process(app);
    const bool demo = parser.isSet("demo") || parser.isSet("screenshot");
    auto preferences = Preferences::load();
    if (parser.isSet("host")) preferences.host = parser.value("host");
    if (parser.isSet("port")) {
        bool ok = false; const auto port = parser.value("port").toInt(&ok);
        if (!ok || port < 1 || port > 65535) { std::cerr << "Ungültiger UDP-Port\n"; return 2; }
        preferences.port = port;
    }
    if (parser.isSet("window")) preferences.desktop = false;
    if (parser.isSet("reset-position")) preferences.position = {-1,-1};
    const auto configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QLockFile lock(configDir + "/instance.lock");
    lock.setStaleLockTime(30000);
    if (!demo && !lock.tryLock(0)) {
        QMessageBox::information(nullptr, "Philips AirControl", "AirControl läuft bereits.\n"
            "Über das Symbol in der Leiste kannst du das Widget anzeigen.");
        return 0;
    }
    const auto backend = QCoreApplication::applicationDirPath() + "/airctrl-backend";
    Desklet widget(preferences, backend, demo);
    if (demo) {
        // Measurements from the user's confirmed AC2729 status; no identifying IDs.
        widget.applyStatus({{"name","Wohnzimmer"},{"modelid","AC2729/10"},
            {"rh",55},{"rhset",50},{"temp",24},{"pm25",1},{"pwr","1"},
            {"rssi",-38},{"func","PH"},{"mode","P"},{"om","s"},{"iaql",1},
            {"cl",false},{"aqil",100},{"uil","1"},{"dt",0}});
    }
    widget.showAndPosition();
    if (parser.isSet("screenshot")) {
        const auto path = parser.value("screenshot");
        QTimer::singleShot(200, &widget, [&widget, &app, path] {
            if (!widget.grab().save(path)) { std::cerr << "PNG konnte nicht gespeichert werden\n"; app.exit(1); }
            else app.quit();
        });
    } else if (!demo) QTimer::singleShot(0, &widget, &Desklet::start);
    return app.exec();
}
