/**
 * @file desklet_rendering.cpp
 * @brief Desklet painting, appearance, metric, and emblem rendering.
 */
#include "desklet.hpp"
#include "desklet_support.hpp"

#include <QFontMetrics>
#include <QLayout>
#include <QPainter>
#include <QPalette>

#include <algorithm>

using namespace desklet_support;

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
    for(int i=0;i<5;++i) {
        QLabel* value=values_[i];
        value->setText(metricText(metricKeys[i],status_.value(metricKeys[i])));
        QPalette palette=value->palette();
        palette.setColor(QPalette::WindowText,color);
        value->setPalette(palette);
        value->setAccessibleName(metricNames[i]+": "+value->text());
    }
    resizeToContent();
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
