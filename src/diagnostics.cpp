#include "diagnostics.hpp"
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QRegularExpression>
#include <cmath>

namespace {
// Protocol references and the limits of their interpretation are documented in
// README.md. Preserve raw values, including JSON types; never infer maintenance
// alarms from unknown codes or turn these descriptions into device commands.
const QHash<QString,QString> descriptions{
    {"pwr", "Ein/Aus-Zustand: \"1\" = eingeschaltet, \"0\" = ausgeschaltet."},
    {"cl", "Kindersicherung: false = aus, true = ein."},
    {"mode", "Betriebsmodus beim AC2729: P = Automatik, A = Allergen, S = Nacht, M = manuelle Lüfterwahl."},
    {"om", "Lüfterstufe: 1, 2, 3; t = Turbo; s = leise/Nacht. Zusammen mit mode auswerten."},
    {"func", "Gerätefunktion: P = Luftreinigung, PH = Luftreinigung mit Befeuchtung."},
    {"rh", "Gemessene relative Luftfeuchtigkeit in Prozent."},
    {"rhset", "Eingestellte Zielfeuchte in Prozent; hier sind 40, 50, 60 und 70 % wählbar."},
    {"temp", "Vom Gerät gemessene Temperatur in °C."},
    {"pm25", "Feinstaubkonzentration PM2,5 in µg/m³: Masse der feinen Partikel pro Kubikmeter Luft."},
    {"iaql", "Philips-Innenraum-Allergenindex (IAI). Ein Indexwert, keine Konzentration in µg/m³."},
    {"aqil", "Helligkeit des Luftqualitäts-Lichtrings, 0 bis 100; 0 = aus, 100 = volle Helligkeit."},
    {"uil", "Beleuchtung der Geräteanzeige: \"0\" = aus, \"1\" = ein."},
    {"dt", "Eingestellter Abschalttimer in Stunden; 0 = kein Abschalttimer."},
    {"dtrs", "Zusätzliches Philips-Statusfeld. Genaue Bedeutung und Einheit sind nicht gesichert."},
    {"ddp", "Ausgewählte Geräteanzeige. In der Referenz: 0 = IAI, 1 = PM2,5, 2 = Gas. Weitere Codes, darunter 3, sind hier nicht gesichert zugeordnet."},
    {"rddp", "Zusätzliches Anzeigefeld von Philips. Genaue Bedeutung und Codezuordnung sind nicht gesichert."},
    {"aqit", "Interner Luftqualitätsindex. Seine Skala ist für dieses Modell nicht gesichert dokumentiert."},
    {"aqit_ext", "Philips-Zusatzfeld zur Luftqualität; genaue Bedeutung und Skala sind nicht gesichert."},
    {"wl", "Gemeldeter Wasserstand bzw. Wasserstatus. Beim AC2729 mit func=PH zeigt wl=0 den Nachfüllhinweis. 100 ist keine gesicherte Messung der Füllmenge in Prozent."},
    {"fltsts0", "Wartungszähler des Vorfilters. Übliche Protokolldeutung: verbleibende Betriebsstunden bis zur Reinigung; mit der Geräteanzeige abgleichen."},
    {"fltsts1", "Restlaufzeitzähler des HEPA-Filters. Die Referenzintegration deutet ihn als verbleibende Betriebsstunden, nicht als Prozentwert."},
    {"fltsts2", "Restlaufzeitzähler des Aktivkohlefilters. Die Referenzintegration deutet ihn als verbleibende Betriebsstunden, nicht als Prozentwert."},
    {"wicksts", "Restlaufzeitzähler des Befeuchterfilters (Wick). Die Referenzintegration deutet ihn als verbleibende Betriebsstunden."},
    {"fltt1", "Typkennung des HEPA-Filters, z.B. A3."},
    {"fltt2", "Typkennung des Aktivkohlefilters, z.B. C7."},
    {"flttotal0", "Gesamtwert zum Vorfilterzähler; Bezugsgröße für eine mögliche Restlaufzeitberechnung."},
    {"flttotal1", "Gesamtwert zum HEPA-Filterzähler; Bezugsgröße für eine mögliche Restlaufzeitberechnung."},
    {"flttotal2", "Gesamtwert zum Aktivkohlefilterzähler; Bezugsgröße für eine mögliche Restlaufzeitberechnung."},
    {"wicktotal", "Gesamtwert zum Befeuchterfilterzähler; Bezugsgröße für eine mögliche Restlaufzeitberechnung."},
    {"err", "Geräteeigener Fehler-/Statuscode. Beim AC2729: 0xC100 (49408) = Wasser fehlt, 0xC001/0xC003 (49153/49155) = Vorfilter reinigen (Referenzintegration). Aus anderen Codes, etwa 0xC054 (49236), wird kein gesicherter Wartungsalarm abgeleitet; keine pauschale Bitmasken-Deutung."},
    {"name", "Vom Gerät gemeldeter frei vergebener Name, z.B. Wohnzimmer."},
    {"type", "Gerätebaureihe, z.B. AC2729."},
    {"modelid", "Vollständige Modellkennung einschließlich Variante, z.B. AC2729/10."},
    {"DeviceId", "Interne Kennung des einzelnen Geräts im Philips-Protokoll."},
    {"ProductId", "Interne Produktkennung im Philips-Protokoll."},
    {"DeviceVersion", "Vom Gerät gemeldete Geräteversionskennung."},
    {"swversion", "Software-/Firmwareversion des Geräts."},
    {"WifiVersion", "Firmwarekennung des WLAN-Moduls."},
    {"range", "Interne Philips-Familien-/Plattformkennung, z.B. MicroMario; keine Entfernungsangabe."},
    {"Runtime", "Interner Laufzeitzähler. Die Referenzintegration interpretiert ihn in Millisekunden; kein bestätigter Lebensdauer-Betriebsstundenzähler."},
    {"rssi", "WLAN-Signalstärke (RSSI), üblicherweise in dBm. Ein weniger negativer Wert bedeutet stärkeren Empfang."},
    {"ConnectType", "Vom Gerät gemeldeter Verbindungsstatus, z.B. Online. Gehört zum letzten Datenempfang und beweist keine aktuell bestehende Widget-Verbindung."},
    {"StatusType", "Art der Geräteantwort, z.B. status für Statusdaten oder control für eine Steuerantwort."},
    {"free_memory", "Vom Gerät gemeldeter freier Speicher. Die Einheit ist für diesen Rohwert nicht gesichert dokumentiert."},
    {"otacheck", "Vermutlich internes Kennzeichen einer Firmware-Update-Prüfung (OTA). Genaue Bedeutung von true/false ist nicht gesichert."},
    {"wifilog", "Vermutlich internes Kennzeichen der WLAN-Protokollierung. Genaue Wirkung ist nicht gesichert dokumentiert."}
};
QString hexCode(const QString& tag,const QJsonValue& value) {
    static const QSet<QString> codes{"err","dtrs","ddp","rddp","aqit","aqit_ext","wl"};
    if(!codes.contains(tag)) return {};
    qulonglong number=0;
    if(value.isString()) {
        static const QRegularExpression decimal("^[0-9]+$");
        if(!decimal.match(value.toString()).hasMatch()) return {};
        bool ok=false; number=value.toString().toULongLong(&ok,10); if(!ok) return {};
    } else if(value.isDouble()) {
        const auto n=value.toDouble();
        if(!std::isfinite(n) || n<0 || std::floor(n)!=n || n>9007199254740991.0) return {};
        number=static_cast<qulonglong>(n);
    } else return {};
    return "0x"+QString::number(number,16).toUpper().rightJustified(4,'0');
}
}

QList<DiagnosticField> describeDeviceFields(const QJsonObject& status) {
    QList<DiagnosticField> fields;
    for(auto entry=status.begin(); entry!=status.end(); ++entry) {
        // Wrapping in an array lets Qt serialize any JSON value, even scalars.
        const auto json=QJsonDocument(QJsonArray{entry.value()}).toJson(QJsonDocument::Compact);
        fields.append({entry.key(),QString::fromUtf8(json.mid(1,json.size()-2)),
            descriptions.value(entry.key(),"Nicht dokumentiertes Philips-Feld. Der Rohwert bleibt unverändert; keine gesicherte Bedeutung oder Einheit verfügbar."),
            hexCode(entry.key(),entry.value())});
    }
    return fields;
}

QString diagnosticFieldReport(const QList<DiagnosticField>& fields) {
    QString text;
    for(const auto& field:fields)
        text+=field.tag+" = "+field.value+(field.hex.isEmpty() ? QString() : " ["+field.hex+"]")+"\n  "+field.description+"\n";
    return text;
}
