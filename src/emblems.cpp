#include "emblems.hpp"
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <cmath>
#include <optional>

namespace {
std::optional<int> integer(const QJsonValue& value) {
    // Missing, null, booleans and malformed strings must never become zero.
    if(!value.isDouble() && !value.isString()) return {};
    bool ok = value.isDouble();
    const auto n = ok ? value.toDouble() : value.toString().toDouble(&ok);
    if(!ok || !std::isfinite(n) || n != std::floor(n) || n < -1000000 || n > 1000000) return {};
    return int(n);
}
bool isAc2729(const QJsonObject& status) {
    const auto model = status.value("modelid").toString().toUpper();
    if(!model.isEmpty()) return model == "AC2729" || model.startsWith("AC2729/");
    return status.value("type").toString().toUpper() == "AC2729";
}
void drawGlyph(QPainter& p, EmblemIcon icon, const QColor& color, const QString& badge) {
    p.setPen(QPen(color, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const auto line = [&](double x, double y, double a, double b) { p.drawLine(QPointF(x,y),QPointF(a,b)); };
    const auto drop = [&](double x, double y, double size, bool filled) {
        QPainterPath q; q.moveTo(x,y);
        q.cubicTo(x-size,y+size*1.25,x-size,y+size*2,x,y+size*2);
        q.cubicTo(x+size,y+size*2,x+size,y+size*1.25,x,y);
        p.save(); p.setBrush(filled ? QBrush(color) : QBrush(Qt::NoBrush)); p.drawPath(q); p.restore();
    };
    const auto text = [&](const QString& value, const QRectF& rect, int pixels, bool bold) {
        QFont f=p.font(); f.setPixelSize(pixels); f.setBold(bold); p.setFont(f);
        p.drawText(rect, Qt::AlignCenter, value);
    };
    switch(icon) {
    case EmblemIcon::Sleep: {
        QPainterPath moon, cut; moon.addEllipse(QRectF(2,2,17,17)); cut.addEllipse(QRectF(9,-1,16,16));
        p.fillPath(moon.subtracted(cut),color);
        line(18,3,18,7); line(16,5,20,5); break;
    }
    case EmblemIcon::Auto:
        p.drawArc(QRectF(3,2,18,19),25*16,135*16); p.drawArc(QRectF(3,2,18,19),205*16,135*16);
        line(3,7,3,3); line(21,16,21,20); text("A",QRectF(4,3,16,17),14,true); break;
    case EmblemIcon::Allergen:
        p.save(); p.translate(10,9); p.setBrush(color); p.setPen(Qt::NoPen);
        for(int n=0;n<8;++n) { p.drawEllipse(QRectF(-1.5,-8,3,6)); p.rotate(45); }
        p.drawEllipse(QPointF(0,0),3,3); p.restore();
        p.drawEllipse(QRectF(13,13,8,8)); line(15,15,19,19); line(19,15,15,19); break;
    case EmblemIcon::Purify:
    case EmblemIcon::Humidify: {
        QPainterPath house; house.moveTo(1,11); house.lineTo(12,1); house.lineTo(23,11);
        house.lineTo(20,11); house.lineTo(20,22); house.lineTo(4,22); house.lineTo(4,11); house.closeSubpath();
        QPainterPath cut;
        if(icon==EmblemIcon::Purify) {
            cut.moveTo(8,17); cut.cubicTo(6,12,13,8,17,7); cut.cubicTo(18,13,14,19,8,17);
        } else {
            cut.moveTo(12,7); cut.cubicTo(4,16,8,19,12,19); cut.cubicTo(16,19,20,16,12,7);
        }
        p.fillPath(house.subtracted(cut),color);
        if(icon==EmblemIcon::Purify) line(8,19,14,12);
        break;
    }
    case EmblemIcon::Filter:
        p.drawRoundedRect(QRectF(2,6,12,16),1,1);
        for(int y=9;y<21;y+=3) { line(4,y,5,y); line(8,y,9,y); }
        p.drawRoundedRect(QRectF(9,2,13,13),1,1); line(12,12,19,5); line(14,5,19,5); line(19,5,19,10); break;
    case EmblemIcon::Water:
        drop(12,2,5,true); p.setPen(QPen(color,2.2)); line(3,14,3,21); line(3,21,21,21); line(21,21,21,14); break;
    case EmblemIcon::Clean:
        p.drawEllipse(QRectF(2,2,17,17));
        for(int y=5;y<=14;y+=3) for(int x=5;x<=14;x+=3) p.drawPoint(QPointF(x,y));
        p.setPen(QPen(color,2)); line(12,20,22,20); line(17,17,17,23); break;
    case EmblemIcon::PM25: text("PM2.5",QRectF(0,2,24,20),8,true); break;
    case EmblemIcon::IAI: text("IAI",QRectF(0,2,24,20),15,false); break;
    case EmblemIcon::Wifi:
        p.setPen(QPen(color,2,Qt::SolidLine,Qt::RoundCap));
        p.drawArc(QRectF(1,5,22,22),45*16,90*16);
        p.drawArc(QRectF(5,9,14,14),45*16,90*16);
        p.drawArc(QRectF(9,13,6,6),45*16,90*16);
        p.setBrush(color); p.drawEllipse(QPointF(12,19),1,1); break;
    case EmblemIcon::ChildLock:
        p.drawArc(QRectF(6,2,10,13),0,180*16); p.setBrush(color); p.drawRoundedRect(QRectF(3,9,16,13),2,2);
        break;
    case EmblemIcon::Fan:
        p.save(); p.translate(9,10); p.setBrush(color); p.setPen(Qt::NoPen);
        for(int i=0;i<3;++i) { QPainterPath q; q.moveTo(0,-1); q.cubicTo(-6,-7,5,-11,4,-5); q.cubicTo(4,-2,1,0,0,-1); p.drawPath(q); p.rotate(120); }
        p.drawEllipse(QPointF(0,0),1.4,1.4); p.restore();
        text(badge,QRectF(15,11,9,13),11,true); break;
    case EmblemIcon::Timer:
        p.drawEllipse(QRectF(2,2,15,15)); line(9.5,4,9.5,9.5); line(9.5,9.5,6,11);
        text(badge,QRectF(11,13,13,11),9,true); break;
    }
}
}

QList<EmblemState> currentEmblems(const QJsonObject& status, bool connected) {
    QList<EmblemState> result;
    const auto add = [&](const QString& id, EmblemIcon icon, const QString& description,
                         const QString& badge=QString(), bool warning=false) {
        result.append({id,icon,description,badge,warning});
    };
    if(status.value("cl").isBool() && status.value("cl").toBool())
        add("lock",EmblemIcon::ChildLock,"Kindersicherung aktiv (cl=true)");
    if(status.value("pwr")=="1") {
        const auto mode=status.value("mode").toString();
        if(mode=="P") add("mode",EmblemIcon::Auto,"Automatischer Modus (mode=P)");
        else if(mode=="S") add("mode",EmblemIcon::Sleep,"Ruhemodus / Nacht (mode=S)");
        else if(mode=="A") add("mode",EmblemIcon::Allergen,"Allergiemodus (mode=A)");
        else if(mode=="M") {
            const auto fan=status.value("om").toString();
            if(fan=="1" || fan=="2" || fan=="3" || fan=="t")
                add("mode",EmblemIcon::Fan,fan=="t" ? "Manuell · Turbo (om=t)" : "Manuell · Lüfterstufe "+fan,
                    fan=="t" ? "T" : fan);
        }
        const auto function=status.value("func").toString();
        if(function=="P") add("function",EmblemIcon::Purify,"Nur Luftreinigung (func=P)");
        else if(function=="PH") add("function",EmblemIcon::Humidify,"2-in-1: Luftreinigung + Befeuchtung (func=PH)");

        // The image is an icon legend, not an alarm bitmask specification.
        // Restrict legacy alarm codes to this model; never decode arbitrary err bits.
        if(isAc2729(status)) {
            const auto err=integer(status.value("err"));
            QStringList replace, clean;
            for(const auto& key : {"fltsts1","fltsts2"}) {
                if(integer(status.value(key))==0) replace.append(QString(key)+"=0");
            }
            if(!replace.isEmpty()) add("filter",EmblemIcon::Filter,
                "Filterwechsel fällig: Restlaufzeitzähler abgelaufen ("+replace.join(", ")+")",{},true);
            if(function=="PH" && (integer(status.value("wl"))==0 || err==49408))
                add("water",EmblemIcon::Water,"Wasser nachfüllen: wl=0 oder bekannter Leerstandscode 49408",{},true);
            for(const auto& key : {"fltsts0","wicksts"}) {
                if(integer(status.value(key))==0) clean.append(QString(key)+"=0");
            }
            if(err==49153 || err==49155) clean.append("Vorfiltercode "+QString::number(*err));
            if(!clean.isEmpty()) add("clean",EmblemIcon::Clean,
                "Reinigung fällig: Vorfilter / Befeuchtungselement ("+clean.join(", ")+")",{},true);
        }
        const auto display=integer(status.value("ddp"));
        if(display==0) add("display",EmblemIcon::IAI,"Geräteanzeige: Innenraumallergenindex IAI (ddp=0)");
        else if(display==1) add("display",EmblemIcon::PM25,"Geräteanzeige: Feinstaub PM2.5 (ddp=1)");
        const auto hours=integer(status.value("dt"));
        if(hours && *hours>=1 && *hours<=12)
            add("timer",EmblemIcon::Timer,"Abschalttimer eingestellt: "+QString::number(*hours)+" h (dt; keine Restzeit)",QString::number(*hours));
    }
    add("wifi",EmblemIcon::Wifi,connected ? "WLAN / Statusverbindung: verbunden" : "Keine aktuelle Statusverbindung",{},!connected);
    return result;
}

Emblem::Emblem(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
}
void Emblem::configure(const EmblemState& state, const QColor& foreground, const QFont& font,
                       bool connected, bool hasStatus) {
    state_=state; setFont(font);
    const int side=qMax(24,QFontMetrics(font).height()+4); setFixedSize(side,side);
    stale_=!connected && state.icon!=EmblemIcon::Wifi;
    disconnected_=!connected && state.icon==EmblemIcon::Wifi;
    ink_=state.warning ? QColor("#d97706") : foreground;
    ink_.setAlpha(stale_ ? 110 : 255);
    QString description=state.description;
    if(stale_) description="Letzter bestätigter Zustand · derzeit offline\n"+description;
    if(disconnected_ && !hasStatus) description+=" · noch kein Status empfangen";
    setAccessibleName(description); setAccessibleDescription(description);
    setToolTip(description+"\nKlick / Rechtsklick: Menü · Ziehen: Verschieben");
    update();
}
void Emblem::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.translate(2,2); p.scale((width()-4)/24.0,(height()-4)/24.0);
    drawGlyph(p,state_.icon,ink_,state_.badge);
    if(disconnected_) {
        p.setPen(QPen(ink_,1.8,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(QPointF(3,3),QPointF(21,21));
    }
}
