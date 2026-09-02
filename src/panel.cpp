#include "panel.hpp"
#include <QPainter>
#include <QPainterPath>

namespace {
void glyph(QPainter& p, PanelIcon icon, const QRectF& box, const QColor& color) {
    p.save(); p.translate(box.topLeft()); p.scale(box.width()/20, box.height()/20);
    p.setPen(QPen(color, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
    auto line = [&](double x, double y, double a, double b) { p.drawLine(QPointF(x,y),QPointF(a,b)); };
    auto drop = [&](double x, double y, double size) {
        QPainterPath q; q.moveTo(x,y); q.cubicTo(x-size,y+size*1.3,x-size,y+size*2,x,y+size*2);
        q.cubicTo(x+size,y+size*2,x+size,y+size*1.3,x,y); p.drawPath(q);
    };
    switch(icon) {
    case PanelIcon::Power:
        p.drawArc(QRectF(4,4,12,12), 135*16, 270*16); line(10,2,10,10); break;
    case PanelIcon::ChildLock:
        p.drawEllipse(QRectF(5,2,5,5)); p.drawArc(QRectF(2,8,11,8),0,180*16);
        p.drawRoundedRect(QRectF(10,11,7,6),1,1); p.drawArc(QRectF(11,7,5,8),0,180*16); break;
    case PanelIcon::Auto: {
        QFont f=p.font(); f.setPixelSize(11); f.setBold(false); p.setFont(f);
        p.drawText(QRectF(3,3,14,14), Qt::AlignCenter,"A");
        p.drawArc(QRectF(2,2,16,16),25*16,130*16); p.drawArc(QRectF(2,2,16,16),205*16,130*16);
        line(2.6,6.8,2.2,3.5); line(17.4,13.2,17.8,16.5); break;
    }
    case PanelIcon::Fan:
        p.translate(10,10); p.setBrush(color); p.setPen(Qt::NoPen);
        for(int i=0;i<3;++i) { QPainterPath q; q.moveTo(0,-1); q.cubicTo(-6,-7,5,-11,4,-5); q.cubicTo(4,-2,1,0,0,-1); p.drawPath(q); p.rotate(120); }
        p.drawEllipse(QPointF(0,0),1.4,1.4); break;
    case PanelIcon::Humidity: drop(7,3,4); drop(14,8,2.5); break;
    case PanelIcon::Light:
        p.drawEllipse(QRectF(6,5,8,8)); line(8,13,8,16); line(12,13,12,16); line(8,16,12,16); line(9,18,11,18);
        line(10,1,10,2.5); line(3,5,4,6); line(16,6,17,5); line(2,10,3.5,10); line(16.5,10,18,10); break;
    case PanelIcon::Function:
        line(2,9,10,2); line(10,2,18,9); line(4,8,4,18); line(4,18,16,18); line(16,18,16,8);
        drop(10,7,3);
        break;
    case PanelIcon::Timer:
        p.drawEllipse(QRectF(3,3,14,14)); line(10,5,10,10); line(10,10,6,11); break;
    }
    p.restore();
}
}
PanelButton::PanelButton(PanelIcon icon, const QString& name, QWidget* parent)
    : QPushButton(parent), icon_(icon) {
    setAccessibleName(name); setToolTip(name); setCursor(Qt::PointingHandCursor); setFocusPolicy(Qt::StrongFocus);
}
void PanelButton::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    if((isEnabled() && (isDown() || underMouse())) || hasFocus()) {
        auto highlight=foreground_; highlight.setAlpha(28); p.setPen(Qt::NoPen); p.setBrush(highlight);
        p.drawRoundedRect(rect().adjusted(1,1,-1,-1),3,3);
    }
    QColor c=foreground_; c.setAlpha(isEnabled() ? 255 : 115);
    p.setPen(QPen(c,0.9)); p.setBrush(Qt::NoBrush); p.drawEllipse(QRectF(width()/2.0-9,height()/2.0-9,18,18));
    glyph(p,icon_,QRectF(width()/2.0-7,height()/2.0-7,14,14),c);
    if(hasFocus()) { p.setPen(QPen(foreground_,1,Qt::DotLine)); p.drawRect(rect().adjusted(1,1,-2,-2)); }
}
