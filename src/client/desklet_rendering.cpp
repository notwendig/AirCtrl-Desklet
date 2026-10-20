/**
 * @file desklet_rendering.cpp
 * @brief Desklet painting, appearance, metric, and emblem rendering.
 */
#include "desklet.hpp"
#include "desklet_support.hpp"
#include "status_colors.hpp"

#include <QFontMetrics>
#include <QLayout>
#include <QPainter>
#include <QPalette>

#include <algorithm>

using namespace desklet_support;

void MetricLabel::paintEvent(QPaintEvent* event) {
    if(textShown_) QLabel::paintEvent(event);
}

void Desklet::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    QColor color=preferences_.background;
    color.setAlphaF((100-preferences_.transparency)/100.0);
    painter.fillRect(rect(),color);
}

void Desklet::resizeToContent() {
    layout()->invalidate();
    layout()->activate();
    setFixedSize(layout()->sizeHint().expandedTo(QSize(287,0)));
}

void Desklet::applyAppearance() {
    while(QLayoutItem* item=valueLayout_->takeAt(0)) delete item;
    int visible=0;
    for(int i=0;i<5;++i) {
        QLabel* value=values_[i];
        value->setFont(preferences_.valueFont);
        const bool show=preferences_.visibleValues.contains(metricKeys[i]);
        value->setVisible(show);
        if(show) {
            valueLayout_->addWidget(value,visible/2,visible%2);
            ++visible;
        }
    }
    valueArea_->setVisible(visible>0);
    for(PanelButton* button:controls_) {
        button->setForeground(preferences_.foreground);
        button->setFont(preferences_.valueFont);
    }
    monitorBar_->setFont(preferences_.valueFont);
    monitorBar_->setLightSurface(statusOnLightSurface(preferences_.foreground));
    monitorBar_->setFixedSize(monitorBar_->sizeHint());
    updateEmblems();
    updateValues();
    updateMonitoring();
    update();
}

void Desklet::saveAppearance() {
    applyAppearance();
    if(!demo_) preferences_.save();
    if(isVisible()) showAndPosition();
}

void Desklet::updateValues() {
    QColor color=preferences_.foreground;
    if(!connected_ && updated_.isValid()) color.setAlpha(140);
    bool anyBlinking=false;
    for(int i=0;i<5;++i) {
        QLabel* value=values_[i];
        value->setText(metricText(metricKeys[i],status_.value(metricKeys[i])));
        const MetricLevel level=metricLevel(metricKeys[i],status_.value(metricKeys[i]),
            status_.value("rhset"),preferences_.metricLimits);
        metricBlinking_[i]=level==MetricLevel::Blinking;
        anyBlinking=anyBlinking || metricBlinking_[i];
        QColor ink=color;
        const bool lightSurface=statusOnLightSurface(preferences_.foreground);
        switch(level) {
        case MetricLevel::Green: ink=statusGreen(lightSurface); break;
        case MetricLevel::Yellow: ink=statusYellow(lightSurface); break;
        case MetricLevel::Red: case MetricLevel::Blinking: ink=statusRed(lightSurface); break;
        case MetricLevel::Unknown: break;
        }
        value->setStyleSheet({});
        QPalette palette=value->palette();
        palette.setColor(QPalette::WindowText,ink);
        value->setPalette(palette);
        value->setAccessibleName(metricNames[i]+": "+value->text());
    }
    if(anyBlinking) {
        if(!metricBlinkTimer_.isActive()) { metricBlinkVisible_=true; metricBlinkTimer_.start(); }
    } else { metricBlinkTimer_.stop(); metricBlinkVisible_=true; }
    updateMetricBlink();
    resizeToContent();
}

void Desklet::updateMetricBlink() {
    for(int i=0;i<5;++i) {
        if(!metricEffects_[i]) continue;
        const qreal fade=!connected_ && updated_.isValid() ? 0.55 : 1.0;
        metricEffects_[i]->setOpacity(fade);
        values_[i]->setTextShown(!metricBlinking_[i] || metricBlinkVisible_);
    }
}

void Desklet::updateEmblems() {
    // Reserve two rows of six emblems; alarms must not resize the whole window.
    const int side=qMax(24,QFontMetrics(preferences_.valueFont).height()+4);
    emblemBar_->setFixedHeight(2*side+4);
    emblemBar_->setMinimumWidth(6*side+5*4);
    while(QLayoutItem* item=emblemLayout_->takeAt(0)) delete item;
    int index=0;
    const QList<EmblemState> active=currentEmblems(status_,connected_);
    for(Emblem* emblem:emblems_) {
        const QList<EmblemState>::const_iterator found=std::find_if(active.begin(),active.end(),[&](const EmblemState& state) {
            return emblem->objectName()=="emblem_"+state.id;
        });
        if(found!=active.end()) emblem->configure(*found,preferences_.foreground,preferences_.valueFont,
                                                connected_,updated_.isValid());
        emblem->setVisible(found!=active.end());
        if(found!=active.end()) {
            emblemLayout_->addWidget(emblem,index/6,index%6);
            ++index;
        }
    }
}
