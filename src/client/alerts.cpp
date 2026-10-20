/**
 * @file alerts.cpp
 * @brief Stable alarm identities, data-age policy, and notification payloads.
 */
#include "alerts.hpp"
#include "emblems.hpp"
#include "status_colors.hpp"
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <functional>

DataFreshness dataFreshness(qint64 seconds, int warningAfter, int staleAfter, int blinkAfter) {
    if(seconds<0) return DataFreshness::Waiting;
    if(seconds>=blinkAfter) return DataFreshness::Blinking;
    if(seconds>=staleAfter) return DataFreshness::Stale;
    return seconds>=warningAfter ? DataFreshness::Aging : DataFreshness::Fresh;
}
QList<Alert> deviceAlerts(const QJsonObject& status) {
    QList<Alert> result;
    // Stable, separate identities avoid both hourly repeat notifications and
    // one acknowledged filter hiding a newly due second filter.
    for(const FilterNotice& filter:filterNotices(status))
        result.append({filter.key,filter.due ? AlertLevel::Error : AlertLevel::Warning,filter.message});
    for(const EmblemState& emblem:currentEmblems(status,true)) {
        if(emblem.warning && emblem.id!="wifi" && emblem.id!="filter")
            result.append({"device-"+emblem.id,AlertLevel::Warning,emblem.description});
    }
    return result;
}
QString alertReport(const QList<Alert>& alerts) {
    QStringList lines;
    for(const Alert& alert:alerts)
        lines.append((alert.level==AlertLevel::Error ? "FEHLER: " : "WARNUNG: ")+alert.message);
    return lines.isEmpty() ? QString("Keine aktiven Alarme.") : lines.join("\n\n");
}
QDBusMessage alarmNotification(const QString& message, bool critical) {
    QDBusMessage request=QDBusMessage::createMethodCall("org.freedesktop.Notifications","/org/freedesktop/Notifications",
        "org.freedesktop.Notifications","Notify");
    const QVariantMap hints{{"urgency",QVariant::fromValue(uchar(critical ? 2 : 1))},{"transient",true}};
    request.setArguments({"Philips AirControl",uint(0),"airctrl-desklet",
        critical ? "AirControl – Fehler" : "AirControl – Warnung",
        message.left(3000).toHtmlEscaped(),QStringList{},hints,12000});
    return request;
}
QList<Alert> AlertLatch::update(const QList<Alert>& active) {
    QSet<QString> keys; QList<Alert> fresh;
    for(const Alert& alert:active) {
        keys.insert(alert.key);
        const int level=int(alert.level);
        if(level>notified_.value(alert.key,0)) fresh.append(alert);
        // A downgrade within the same unresolved incident does not re-arm.
        notified_[alert.key]=qMax(level,notified_.value(alert.key,0));
    }
    for(QMap<QString,int>::iterator i=notified_.begin();i!=notified_.end();) {
        if(!keys.contains(i.key())) { acknowledged_.remove(i.key()); i=notified_.erase(i); }
        else ++i;
    }
    return fresh;
}
void AlertLatch::acknowledge(const QList<Alert>& active) {
    for(const Alert& alert:active) acknowledged_[alert.key]=int(alert.level);
}
bool AlertLatch::acknowledged(const Alert& alert) const {
    return acknowledged_.value(alert.key,0)>=int(alert.level);
}

MonitorBar::MonitorBar(QWidget* parent) : QWidget(parent) {
    setObjectName("monitorBar"); setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
    blinkTimer_.setInterval(700);
    connect(&blinkTimer_,&QTimer::timeout,this,[this] { blinkVisible_=!blinkVisible_; update(); });
}
int MonitorBar::diameter() const {
    return qMax(26,qCeil(QFontMetricsF(font()).height()*1.4));
}
QSize MonitorBar::sizeHint() const {
    return {diameter()+qMax(52,2*diameter())+6,diameter()};
}
QRectF MonitorBar::ageCircle() const { return QRectF(0,0,qMax(52,2*diameter()),diameter()).adjusted(1,1,-1,-1); }
QRectF MonitorBar::alarmCircle() const { return QRectF(ageCircle().right()+7,1,diameter()-2,diameter()-2); }
QColor MonitorBar::ageColor() const {
    switch(freshness_) {
    case DataFreshness::Fresh: return statusGreen(lightSurface_);
    case DataFreshness::Aging: return statusYellow(lightSurface_);
    case DataFreshness::Stale: case DataFreshness::Blinking: return statusRed(lightSurface_);
    case DataFreshness::Waiting: return QColor(lightSurface_ ? "#666666" : "#d8d8d8");
    }
    return QColor("#d8d8d8");
}
QColor MonitorBar::alarmColor() const {
    for(const Alert& alert:alerts_) if(alert.level==AlertLevel::Error) return QColor("#e74c3c");
    return alerts_.isEmpty() ? QColor("#e4e4e4") : QColor("#f1c40f");
}
QString MonitorBar::alarmText() const {
    if(alerts_.isEmpty()) return "Keine Alarme";
    int errors=0;
    for(const Alert& alert:alerts_) if(alert.level==AlertLevel::Error) ++errors;
    const int warnings=alerts_.size()-errors;
    QString text;
    if(errors && warnings) text=QString::number(errors)+" F / "+QString::number(warnings)+" W";
    else if(errors) text=QString::number(errors)+" Fehler";
    else text=QString::number(warnings)+(warnings==1 ? " Warnung" : " Warnungen");
    if(acknowledged_) text+=" (Q)";
    return text;
}
void MonitorBar::setState(const QDateTime& receivedAt, DataFreshness freshness, const QList<Alert>& alerts,
                         bool acknowledged, const QString& detail) {
    const QSize old=sizeHint();
    receivedAt_=receivedAt; freshness_=freshness; alerts_=alerts; acknowledged_=acknowledged;
    if(freshness_==DataFreshness::Blinking) {
        if(!blinkTimer_.isActive()) { blinkVisible_=true; blinkTimer_.start(); }
    } else { blinkTimer_.stop(); blinkVisible_=true; }
    setAccessibleName("Letzte Statusmeldung: "+ageText()+" · "+alarmText());
    setAccessibleDescription(detail); setToolTip(detail+
        "\nLinksklick: Alarme · Rechtsklick: Menü · Ziehen: Verschieben");
    if(sizeHint()!=old) updateGeometry();
    update();
}
void MonitorBar::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const QRectF age=ageCircle(), alarm=alarmCircle();
    const QColor ink("#151515");
    p.setPen(Qt::NoPen);
    p.setBrush(alarmColor()); p.drawEllipse(alarm);
    const std::function<void(const QString&,const QRectF&,qreal,const QColor&)> text=
        [&](const QString& value,const QRectF& area,qreal scale,const QColor& color) {
        QFont f=font();
        const qreal pixels=QFontMetricsF(f).height()*0.82*scale;
        f.setPixelSize(qMax(1,qRound(pixels)));
        const QFontMetricsF metrics(f);
        const qreal ratio=std::min({1.0,area.width()/qMax(1.0,metrics.horizontalAdvance(value)),
                               area.height()/qMax(1.0,metrics.height())});
        f.setPixelSize(qMax(1,qFloor(f.pixelSize()*ratio)));
        p.setFont(f); p.setPen(color); p.drawText(area,Qt::AlignCenter,value);
    };
    const std::function<QRectF(const QRectF&,qreal,qreal)> zone=[](const QRectF& circle,qreal top,qreal h) {
        return QRectF(circle.left()+circle.width()*0.12,circle.top()+circle.height()*top,
                      circle.width()*0.76,circle.height()*h);
    };
    if(blinkVisible_) text(ageText(),zone(age,0.16,0.68),1.0,ageColor());
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
    if(!alerts_.isEmpty()) text(QString::number(alerts_.size())+(acknowledged_ ? "Q" : ""),zone(alarm,0.59,0.30),0.78,ink);
}
