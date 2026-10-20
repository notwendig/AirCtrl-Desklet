/**
 * @file preferences.cpp
 * @brief Validated per-user settings and desktop autostart persistence.
 */
#include "preferences.hpp"
#include "ipc.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

Preferences::Preferences()
    : serverHost(defaultAirctrlServerHost()), serverPort(defaultAirctrlServerPort()) {}

Preferences Preferences::load() {
    QSettings s;
    Preferences p;
    p.serverHost = s.value("server/host", p.serverHost).toString().trimmed();
    if(p.serverHost.isEmpty()) p.serverHost=defaultAirctrlServerHost();
    p.serverPort = s.value("server/port", p.serverPort).toInt();
    if (p.serverPort < 1 || p.serverPort > 65535) p.serverPort = defaultAirctrlServerPort();
    p.serverReconnectSeconds = qBound(1, s.value("server/reconnectSeconds", 10).toInt(), 300);
    p.ageWarningSeconds=qBound(1,s.value("alarms/warningMinutes",15).toInt(),1439)*60;
    p.ageStaleSeconds=qBound(p.ageWarningSeconds/60+1,s.value("alarms/staleMinutes",30).toInt(),1440)*60;
    p.ageBlinkSeconds=qBound(p.ageStaleSeconds/60+1,s.value("alarms/blinkMinutes",120).toInt(),1441)*60;
    MetricLimits& m=p.metricLimits;
    m.humidityGreenDelta=qBound(0,s.value("metrics/humidityGreenDelta",m.humidityGreenDelta).toInt(),98);
    m.humidityYellowDelta=qBound(m.humidityGreenDelta+1,s.value("metrics/humidityYellowDelta",m.humidityYellowDelta).toInt(),99);
    m.humidityRedDelta=qBound(m.humidityYellowDelta+1,s.value("metrics/humidityRedDelta",m.humidityRedDelta).toInt(),100);
    m.pmGreenHigh=qBound(0,s.value("metrics/pmGreenHigh",m.pmGreenHigh).toInt(),997);
    m.pmYellowHigh=qBound(m.pmGreenHigh+1,s.value("metrics/pmYellowHigh",m.pmYellowHigh).toInt(),998);
    m.pmRedHigh=qBound(m.pmYellowHigh+1,s.value("metrics/pmRedHigh",m.pmRedHigh).toInt(),999);
    p.desktopAlarms=s.value("alarms/desktop",true).toBool();
    p.alarmSound=s.value("alarms/sound",false).toBool();
    p.desktop = s.value("window/desktop", true).toBool();
    p.hideDecoration = s.value("window/hideDecoration", p.desktop).toBool();
    p.locked = s.value("window/locked", false).toBool();
    p.position = s.value("window/position", p.position).toPoint();
    const QColor background(s.value("appearance/background", p.background.name()).toString());
    const QColor foreground(s.value("appearance/foreground", p.foreground.name()).toString());
    if (background.isValid()) p.background = background;
    if (foreground.isValid()) p.foreground = foreground;
    p.transparency = qBound(0, s.value("appearance/transparency", p.transparency).toInt(), 100);
    QFont font;
    if (font.fromString(s.value("appearance/font", p.valueFont.toString()).toString())) {
        font.setPointSizeF(qBound(6.0, font.pointSizeF() > 0 ? font.pointSizeF() : 10.0, 48.0));
        p.valueFont = font;
    }
    p.visibleValues = s.value("appearance/values", p.visibleValues).toStringList();
    const QStringList allowed{"rh", "rhset", "temp", "pm25", "iaql"};
    for (QStringList::iterator i = p.visibleValues.begin(); i != p.visibleValues.end();) {
        if (!allowed.contains(*i)) i = p.visibleValues.erase(i); else ++i;
    }
    p.visibleValues.removeDuplicates();
    return p;
}
void Preferences::save() const {
    QSettings s;
    s.setValue("server/host", serverHost);
    s.setValue("server/port", serverPort);
    s.setValue("server/reconnectSeconds", serverReconnectSeconds);
    s.remove("device/host");
    s.remove("device/port");
    s.remove("device/interval");
    s.setValue("alarms/warningMinutes",ageWarningSeconds/60);
    s.setValue("alarms/staleMinutes",ageStaleSeconds/60);
    s.setValue("alarms/blinkMinutes",ageBlinkSeconds/60);
    s.remove("alarms/warningSeconds");
    s.remove("alarms/staleSeconds");
    s.setValue("metrics/humidityGreenDelta",metricLimits.humidityGreenDelta);
    s.setValue("metrics/humidityYellowDelta",metricLimits.humidityYellowDelta);
    s.setValue("metrics/humidityRedDelta",metricLimits.humidityRedDelta);
    s.setValue("metrics/pmGreenHigh",metricLimits.pmGreenHigh);
    s.setValue("metrics/pmYellowHigh",metricLimits.pmYellowHigh);
    s.setValue("metrics/pmRedHigh",metricLimits.pmRedHigh);
    s.setValue("alarms/desktop",desktopAlarms);
    s.setValue("alarms/sound",alarmSound);
    s.remove("automation/enabled"); // v1.07: enablement belongs to the server state
    s.setValue("window/desktop", desktop);
    s.setValue("window/hideDecoration", hideDecoration);
    s.setValue("window/locked", locked);
    s.setValue("window/position", position);
    s.setValue("appearance/background", background.name());
    s.setValue("appearance/foreground", foreground.name());
    s.setValue("appearance/transparency", transparency);
    s.setValue("appearance/font", valueFont.toString());
    s.setValue("appearance/values", visibleValues);
}
QString autostartPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/airctrl-desklet.desktop";
}
bool setAutostart(bool enabled, QString* error) {
    const QString path = autostartPath();
    if (!enabled) {
        if (!QFile::exists(path) || QFile::remove(path)) return true;
        if (error) *error = "Autostart-Datei konnte nicht entfernt werden.";
        return false;
    }
    const QString executable = QCoreApplication::applicationFilePath();
    QString escaped = executable;
    // Desktop Entry Exec quoting (not shell quoting); %% is a literal percent.
    escaped.replace('\\', "\\\\\\\\");
    escaped.replace('"', "\\\\\"");
    escaped.replace('`', "\\\\`");
    escaped.replace('$', "\\\\$");
    escaped.replace('%', "%%");
    if (escaped.contains('\n') || escaped.contains('\r')) {
        if (error) *error = "Unzulässiger Zeilenumbruch im Programmpfad.";
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray content = QString("[Desktop Entry]\nType=Application\nName=Philips AirControl\n"
        "Comment=Philips-Luftreiniger auf dem Desktop\nExec=\"%1\"\n"
        "Icon=airctrl-desklet\nTerminal=false\nX-GNOME-Autostart-enabled=true\n"
        "X-GNOME-Autostart-Delay=5\n").arg(escaped).toUtf8();
    if (file.write(content) != content.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
