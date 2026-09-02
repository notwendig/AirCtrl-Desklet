#include "preferences.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

Preferences Preferences::load() {
    QSettings s;
    Preferences p;
    p.host = s.value("device/host", p.host).toString();
    p.port = s.value("device/port", p.port).toInt();
    if (p.port < 1 || p.port > 65535) p.port = 5683;
    p.interval = qBound(5, s.value("device/interval", p.interval).toInt(), 300);
    p.desktop = s.value("window/desktop", true).toBool();
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
    for (auto i = p.visibleValues.begin(); i != p.visibleValues.end();) {
        if (!allowed.contains(*i)) i = p.visibleValues.erase(i); else ++i;
    }
    p.visibleValues.removeDuplicates();
    return p;
}
void Preferences::save() const {
    QSettings s;
    s.setValue("device/host", host);
    s.setValue("device/port", port);
    s.setValue("device/interval", interval);
    s.setValue("window/desktop", desktop);
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
    const auto path = autostartPath();
    if (!enabled) {
        if (!QFile::exists(path) || QFile::remove(path)) return true;
        if (error) *error = "Autostart-Datei konnte nicht entfernt werden.";
        return false;
    }
    const auto executable = QCoreApplication::applicationFilePath();
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
    const auto content = QString("[Desktop Entry]\nType=Application\nName=Philips AirControl\n"
        "Comment=Philips-Luftreiniger auf dem Desktop\nExec=\"%1\"\n"
        "Icon=airctrl-desklet\nTerminal=false\nX-GNOME-Autostart-enabled=true\n"
        "X-GNOME-Autostart-Delay=5\n").arg(escaped).toUtf8();
    if (file.write(content) != content.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
