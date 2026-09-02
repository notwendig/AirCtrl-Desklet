#include "alerts.hpp"
#include "emblems.hpp"
#include <QFontMetrics>
#include <QPainter>
#include <QSet>
#include <QStringList>

DataFreshness dataFreshness(qint64 seconds, bool failed, int warningAfter, int staleAfter) {
    if(failed) return DataFreshness::Disconnected;
    if(seconds<0) return DataFreshness::Waiting;
    if(seconds>=staleAfter) return DataFreshness::Stale;
    return seconds>=warningAfter ? DataFreshness::Aging : DataFreshness::Fresh;
}
QList<Alert> deviceAlerts(const QJsonObject& status) {
    QList<Alert> result;
    for(const auto& emblem:currentEmblems(status,true)) {
        if(emblem.warning && emblem.id!="wifi")
            result.append({"device-"+emblem.id,AlertLevel::Warning,emblem.description});
    }
    return result;
}
QString alertReport(const QList<Alert>& alerts) {
    QStringList lines;
    for(const auto& alert:alerts)
        lines.append((alert.level==AlertLevel::Error ? "FEHLER: " : "WARNUNG: ")+alert.message);
    return lines.isEmpty() ? QString("Keine aktiven Alarme.") : lines.join("\n\n");
}
QDBusMessage alarmNotification(const QString& message, bool critical) {
    auto request=QDBusMessage::createMethodCall("org.freedesktop.Notifications","/org/freedesktop/Notifications",
        "org.freedesktop.Notifications","Notify");
    const QVariantMap hints{{"urgency",QVariant::fromValue(uchar(critical ? 2 : 1))},{"transient",true}};
    request.setArguments({"Philips AirControl",uint(0),"airctrl-desklet",
        critical ? "AirControl – Fehler" : "AirControl – Warnung",
        message.left(3000).toHtmlEscaped(),QStringList{},hints,12000});
    return request;
}
QList<Alert> AlertLatch::update(const QList<Alert>& active) {
    QSet<QString> keys; QList<Alert> fresh;
    for(const auto& alert:active) {
        keys.insert(alert.key);
        const auto level=int(alert.level);
        if(level>notified_.value(alert.key,0)) fresh.append(alert);
        // A downgrade within the same unresolved incident does not re-arm.
        notified_[alert.key]=qMax(level,notified_.value(alert.key,0));
    }
    for(auto i=notified_.begin();i!=notified_.end();) {
        if(!keys.contains(i.key())) { acknowledged_.remove(i.key()); i=notified_.erase(i); }
        else ++i;
    }
    return fresh;
}
void AlertLatch::acknowledge(const QList<Alert>& active) {
    for(const auto& alert:active) acknowledged_[alert.key]=int(alert.level);
}
bool AlertLatch::acknowledged(const Alert& alert) const {
    return acknowledged_.value(alert.key,0)>=int(alert.level);
}

MonitorBar::MonitorBar(QWidget* parent) : QWidget(parent) {
    setObjectName("monitorBar"); setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
}
int MonitorBar::ageWidth() const {
    const QFontMetrics metrics(font());
    return qMax(metrics.horizontalAdvance("999999 s"),metrics.horizontalAdvance(ageText()))+18;
}
QSize MonitorBar::sizeHint() const {
    const QFontMetrics metrics(font());
    return {ageWidth()+6+metrics.horizontalAdvance("2 Warnungen (Q)")+18,qMax(24,metrics.height()+8)};
}
QColor MonitorBar::ageColor() const {
    switch(freshness_) {
    case DataFreshness::Fresh: return QColor("#2ecc71");
    case DataFreshness::Aging: return QColor("#f1c40f");
    case DataFreshness::Stale: case DataFreshness::Disconnected: return QColor("#e74c3c");
    case DataFreshness::Waiting: return QColor("#d8d8d8");
    }
    return QColor("#d8d8d8");
}
QString MonitorBar::alarmText() const {
    if(alerts_.isEmpty()) return "Keine Alarme";
    int errors=0;
    for(const auto& alert:alerts_) if(alert.level==AlertLevel::Error) ++errors;
    const int warnings=alerts_.size()-errors;
    QString text;
    if(errors && warnings) text=QString::number(errors)+" F / "+QString::number(warnings)+" W";
    else if(errors) text=QString::number(errors)+" Fehler";
    else text=QString::number(warnings)+(warnings==1 ? " Warnung" : " Warnungen");
    if(acknowledged_) text+=" (Q)";
    return text;
}
void MonitorBar::setState(qint64 seconds, DataFreshness freshness, const QList<Alert>& alerts,
                         bool acknowledged, const QString& detail) {
    const auto old=sizeHint();
    seconds_=seconds; freshness_=freshness; alerts_=alerts; acknowledged_=acknowledged;
    setAccessibleName("Datenalter: "+ageText()+" · "+alarmText());
    setAccessibleDescription(detail); setToolTip(detail+
        "\nLinksklick: Alarme · Rechtsklick: Menü · Ziehen: Verschieben");
    if(sizeHint()!=old) updateGeometry();
    update();
}
void MonitorBar::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(ageColor());
    const QRectF age(0,0,ageWidth(),height());
    p.drawRoundedRect(age,4,4); p.setPen(QColor("#151515")); p.drawText(age,Qt::AlignCenter,ageText());
    auto color=QColor("#e4e4e4");
    if(!alerts_.isEmpty()) color=QColor("#f1c40f");
    for(const auto& alert:alerts_) if(alert.level==AlertLevel::Error) color=QColor("#e74c3c");
    const QRectF alarm(age.right()+6,0,qMax(0.0,width()-age.right()-6),height());
    p.setPen(Qt::NoPen); p.setBrush(color); p.drawRoundedRect(alarm,4,4);
    p.setPen(QColor("#151515"));
    p.drawText(alarm,Qt::AlignCenter,fontMetrics().elidedText(alarmText(),Qt::ElideRight,qMax(0,int(alarm.width())-8)));
}
