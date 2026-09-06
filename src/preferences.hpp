#pragma once
#include <QString>
#include <QPoint>
#include <QColor>
#include <QFont>
#include <QStringList>
struct Preferences {
    QString host = "192.168.77.5";
    int port = 5683;
    int interval = 10;
    int ageWarningSeconds = 45;
    int ageStaleSeconds = 90;
    bool desktopAlarms = true;
    bool alarmSound = false;
    bool automationEnabled = false;
    bool desktop = true;
    bool hideDecoration = true;
    bool locked = false;
    QPoint position{-1, -1};
    QColor background{"#f1f1f1"};
    QColor foreground{"#222222"};
    int transparency = 0; // background only: 0 = opaque, 100 = transparent
    QFont valueFont{"DejaVu Sans", 10};
    QStringList visibleValues{"rh", "rhset", "temp", "pm25"};
    static Preferences load();
    void save() const;
};
QString autostartPath();
bool setAutostart(bool enabled, QString* error = nullptr);
