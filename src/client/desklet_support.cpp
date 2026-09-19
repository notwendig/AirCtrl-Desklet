/**
 * @file desklet_support.cpp
 * @brief Shared presentation helpers for the Desklet translation units.
 */
#include "desklet_support.hpp"

#include <QJsonArray>

#include <cmath>

namespace desklet_support {

const QStringList metricKeys{"rh","rhset","temp","pm25","iaql"};
const QStringList metricNames{"Luftfeuchtigkeit","Zielfeuchte","Temperatur","PM2,5","IAI"};

QString metricText(const QString& key, const QJsonValue& value) {
    bool ok=value.isDouble();
    const double n=ok ? value.toDouble() : value.toString().toDouble(&ok);
    const bool humidity=key=="rh" || key=="rhset";
    ok=ok && std::isfinite(n) && n<=(humidity ? 100 : 999) && n>=(key=="temp" ? -100 : 0);
    const QString number=ok ? QString::number(n,'f',n==std::floor(n) ? 0 : 1) : QString("—");
    if(key=="rh") return "Feuchte " + number + (ok ? " %" : "");
    if(key=="rhset") return "Ziel " + number + (ok ? " %" : "");
    if(key=="temp") return number + " °C";
    if(key=="pm25") return "PM2,5 " + number + (ok ? " µg/m³" : "");
    return "IAI " + number;
}

bool powerKnown(const QJsonObject& state) {
    const QString value=state.value("pwr").toString();
    return value=="0" || value=="1";
}

QString endpointText(QString host, int port) {
    host=host.trimmed();
    if(host.contains(':') && !(host.startsWith('[') && host.endsWith(']'))) host="["+host+"]";
    return host+":"+QString::number(port);
}

QString automationStateLabel(const QJsonObject& state) {
    if (state.isEmpty()) return "Serverstatus unbekannt";
    if (!state.value("error").toString().isEmpty()) return "Fehler";
    if (!state.value("enabled").toBool()) return "aus";
    if (state.value("manual_override").toBool()) return "manuell gesperrt";
    return state.value("loaded").toBool() ? "aktiv" : "Fehler";
}

QString automationDiagnostics(const QJsonObject& state) {
    if (state.isEmpty()) return "Serverstatus noch nicht empfangen.\n";
    QString text="Ausführung = airctrl-server\n"
        "Zustand = "+automationStateLabel(state)+"\n"
        "Lua-Version = "+state.value("lua_version").toString("unbekannt")+" (im Server eingebettet)\n"
        "Skript auf dem Server = "+state.value("script_path").toString("unbekannt")+"\n"
        "Revision = "+QString::number(state.value("revision").toVariant().toULongLong())+"\n"
        "Zeitpläne = "+QString::number(state.value("schedule_count").toInt())+"\n"
        "Letztes Ereignis = "+state.value("last_event").toString("—")+"\n"
        "Letzte Aktion = "+state.value("last_action").toString("—")+"\n";
    if(state.value("manual_override").toBool())
        text+="Automatik-Sperre = manuelle Geräteeinstellung; Power-Taste zum Freigeben\n";
    if(!state.value("error").toString().isEmpty())
        text+="Letzter Lua-Fehler = "+state.value("error").toString()+"\n";
    const QJsonArray log=state.value("log").toArray();
    if(!log.isEmpty()) {
        text+="Lua-Protokoll:\n";
        for(const QJsonValue& entry:log) text+="  "+entry.toString()+"\n";
    }
    return text;
}

} // namespace desklet_support
