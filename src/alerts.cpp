#include "alerts.hpp"
#include "emblems.hpp"
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
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
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
}
int MonitorBar::diameter() const {
    return qMax(40,qCeil(QFontMetricsF(font()).height()*2.4));
}
QSize MonitorBar::sizeHint() const {
    return {2*diameter()+6,diameter()};
}
QRectF MonitorBar::ageCircle() const { return QRectF(0,0,diameter(),diameter()).adjusted(1,1,-1,-1); }
QRectF MonitorBar::alarmCircle() const { return ageCircle().translated(diameter()+6,0); }
QColor MonitorBar::ageColor() const {
    switch(freshness_) {
    case DataFreshness::Fresh: return QColor("#2ecc71");
    case DataFreshness::Aging: return QColor("#f1c40f");
    case DataFreshness::Stale: case DataFreshness::Disconnected: return QColor("#e74c3c");
    case DataFreshness::Waiting: return QColor("#d8d8d8");
    }
    return QColor("#d8d8d8");
}
QColor MonitorBar::alarmColor() const {
    for(const auto& alert:alerts_) if(alert.level==AlertLevel::Error) return QColor("#e74c3c");
    return alerts_.isEmpty() ? QColor("#e4e4e4") : QColor("#f1c40f");
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
    const auto age=ageCircle(), alarm=alarmCircle();
    const QColor ink("#151515");
    p.setPen(Qt::NoPen); p.setBrush(ageColor()); p.drawEllipse(age);
    p.setBrush(alarmColor()); p.drawEllipse(alarm);
    const auto text=[&](const QString& value,const QRectF& area,qreal scale=1.0) {
        QFont f=font();
        const qreal pixels=QFontMetricsF(f).height()*0.82*scale;
        f.setPixelSize(qMax(1,qRound(pixels)));
        const qreal ratio=qMin(1.0,area.width()/qMax(1.0,QFontMetricsF(f).horizontalAdvance(value)));
        f.setPixelSize(qMax(1,qFloor(f.pixelSize()*ratio)));
        p.setFont(f); p.setPen(ink); p.drawText(area,Qt::AlignCenter,value);
    };
    const auto zone=[](const QRectF& circle,qreal top,qreal h) {
        return QRectF(circle.left()+circle.width()*0.12,circle.top()+circle.height()*top,
                      circle.width()*0.76,circle.height()*h);
    };
    text(seconds_<0 ? QString("—") : QString::number(seconds_),zone(age,0.13,0.45));
    text("s",zone(age,0.55,0.30),0.72);
    // Native vectors keep the OK/bell symbols independent of installed fonts.
    p.save(); p.translate(alarm.topLeft()); p.scale(alarm.width()/40,alarm.height()/40);
    p.setPen(QPen(ink,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
    if(alerts_.isEmpty()) {
        p.drawLine(QPointF(11,20),QPointF(17,26)); p.drawLine(QPointF(17,26),QPointF(29,13));
    } else {
        QPainterPath bell; bell.moveTo(13,19); bell.lineTo(15,16); bell.lineTo(15,12);
        bell.cubicTo(15,5,25,5,25,12); bell.lineTo(25,16); bell.lineTo(27,19); bell.closeSubpath();
        p.drawPath(bell); p.drawLine(QPointF(19,22),QPointF(21,22));
    }
    p.restore();
    if(!alerts_.isEmpty()) text(QString::number(alerts_.size())+(acknowledged_ ? "Q" : ""),zone(alarm,0.59,0.30),0.78);
}
