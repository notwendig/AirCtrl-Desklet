/**
 * @file desklet_events.cpp
 * @brief Desklet window behavior, input handling, and context menu.
 */
#include "desklet.hpp"
#include "desklet_support.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QFontDialog>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QScreen>
#include <QTimer>
#include <QWindow>

using namespace desklet_support;

void Desklet::applyWindowMode() {
    const bool x11 = QGuiApplication::platformName() == "xcb";
    const bool desktop = preferences_.desktop && x11 && !waylandSession_;
    // Dock + BELOW is Muffin's BOTTOM layer: above Nemo, below normal windows.
    // Desktop type uses the same layer as Nemo and lets its icons cover us.
    // Do not use Qt::Tool here: utility windows can be promoted with their group.
    setAttribute(Qt::WA_X11NetWmWindowTypeDesktop, false);
    setAttribute(Qt::WA_X11NetWmWindowTypeDock, false);
    Qt::WindowFlags flags=demo_ && !desktop && !waylandSession_ ? Qt::Tool : Qt::Window;
    if(preferences_.hideDecoration) flags|=Qt::FramelessWindowHint;
    if(desktop) flags|=Qt::WindowStaysOnBottomHint;
    setWindowFlags(flags);
    // Dock windows have no WM decoration. Use a normal BELOW window when a
    // title bar is requested; restoring frameless mode restores the Dock layer.
    setAttribute(Qt::WA_X11NetWmWindowTypeDock, desktop && preferences_.hideDecoration);
    // No strut is set: this is not a panel and reserves no screen area.
}
void Desklet::setDecorationHidden(bool hidden) {
    if(preferences_.hideDecoration==hidden) return;
    if(!waylandSession_) preferences_.position=pos();
    preferences_.hideDecoration=hidden;
    if(!demo_) preferences_.save();
    applyWindowMode(); showAndPosition();
}
void Desklet::start() {
    if (demo_) return;
    controller_.configure(preferences_.serverHost,preferences_.serverPort,preferences_.serverReconnectSeconds);
    controller_.start();
}
void Desklet::showAndPosition() {
    adjustSize();
    if (waylandSession_) { show(); return; } // The compositor owns global placement.
    const QPoint desired = preferences_.position;
    QScreen* target = nullptr;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->availableGeometry().contains(desired)) { target = screen; break; }
    }
    if (!target) target = QGuiApplication::primaryScreen();
    if (target) {
        const QRect available = target->availableGeometry();
        QPoint point = desired;
        if (!available.contains(point)) point = available.topRight() - QPoint(width()+24, -24);
        point.setX(qBound(available.left(), point.x(), qMax(available.left(), available.right()-width()+1)));
        point.setY(qBound(available.top(), point.y(), qMax(available.top(), available.bottom()-height()+1)));
        move(point);
    }
    show();
}
bool Desklet::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ContextMenu) {
        // Native context events may arrive on press or release; a right-button
        // sequence is handled on release below and must only open one menu.
        if (!rightPressed_) {
            const QContextMenuEvent* context=static_cast<QContextMenuEvent*>(event);
            requestMenu(context->reason()==QContextMenuEvent::Keyboard ? mapToGlobal(rect().center()) : context->globalPos());
        }
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        const QMouseEvent* mouse=static_cast<QMouseEvent*>(event);
        if (mouse->button()==Qt::RightButton) { rightPressed_=true; return true; }
        // Device buttons keep their left-click action. Values and free space
        // use the left button only for moving; menus are right-click only.
        if (mouse->button()==Qt::LeftButton && !qobject_cast<QAbstractButton*>(watched)) {
            leftPressed_=true; mouseMoved_=false;
            pressPosition_=mouse->globalPosition().toPoint(); dragOffset_=pressPosition_-pos();
            return true;
        }
    } else if (event->type()==QEvent::MouseMove && leftPressed_) {
        const QPoint global=static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        mouseMoved_=mouseMoved_ || (global-pressPosition_).manhattanLength()>=QApplication::startDragDistance();
        if (mouseMoved_ && !preferences_.locked && (preferences_.desktop || preferences_.hideDecoration)) {
            if (waylandSession_) {
                // Called while the original left-button press is still active:
                // Wayland requires its input serial for a compositor move.
                leftPressed_=false;
                if (!startNativeMove()) qWarning("AirControl: Fenstermanager hat das Verschieben nicht angenommen");
            } else move(global-dragOffset_);
        }
        return true;
    } else if (event->type()==QEvent::MouseButtonRelease) {
        const QMouseEvent* mouse=static_cast<QMouseEvent*>(event);
        if (mouse->button()==Qt::RightButton) {
            rightPressed_=false; requestMenu(mouse->globalPosition().toPoint()); return true;
        }
        if (mouse->button()==Qt::LeftButton && leftPressed_) {
            leftPressed_=false;
            if (mouseMoved_) {
                if (!preferences_.locked && (preferences_.desktop || preferences_.hideDecoration)) rememberPosition();
            } else if(watched==monitorBar_) QTimer::singleShot(0,this,&Desklet::showAlarms);
            return true;
        }
    }
    return false;
}
bool Desklet::startNativeMove() {
    return windowHandle() && windowHandle()->startSystemMove();
}
void Desklet::rememberPosition() {
    if (!demo_) {
        if (!waylandSession_) preferences_.position=pos();
        preferences_.save();
    }
}
void Desklet::contextMenuEvent(QContextMenuEvent* event) { event->accept(); requestMenu(event->globalPos()); }
void Desklet::requestMenu(const QPoint& point) {
    if (menuPending_ || menuOpen_) return;
    menuPending_=true;
    // Finish the mouse release first so it cannot immediately close the popup.
    QTimer::singleShot(0,this,[this,point] { menuPending_=false; openMenu(point); });
}
void Desklet::openMenu(const QPoint& point) {
    if (menuOpen_) return;
    QScopedValueRollback<bool> guard(menuOpen_,true);
    qInfo("AirControl: Kontextmenü angefordert");
    QMenu menu(this); menu.setObjectName("deskletContextMenu");
    const QString connection=demo_ ? QString("Vorschau") : connected_ ? QString("Verbunden") : QString("Keine Verbindung");
    menu.addSection(status_.value("name").toString("AirControl")+" · "+connection);
    QAction* decoration=menu.addAction("Fensterdekoration ausblenden");
    decoration->setObjectName("hideWindowDecoration"); decoration->setCheckable(true);
    decoration->setChecked(preferences_.hideDecoration);
    connect(decoration,&QAction::triggered,this,[this](bool hidden) {
        // Changing native flags hides/recreates the window. Do it after menu exec.
        QTimer::singleShot(0,this,[this,hidden] { setDecorationHidden(hidden); });
    });
    QMenu* appearance=menu.addMenu("Darstellung");
    appearance->addAction("Hintergrundfarbe …",this,[this] {
        const QColor color=QColorDialog::getColor(preferences_.background,this,"Hintergrundfarbe");
        if(color.isValid()) { preferences_.background=color; saveAppearance(); }
    });
    QAction* transparency=appearance->addAction("Hintergrundtransparenz …",this,[this] {
        bool ok=false;
        const int percent=QInputDialog::getInt(this,"Hintergrundtransparenz", "Transparenz in % (0 = deckend, 100 = durchsichtig):",
                                               preferences_.transparency,0,100,5,&ok);
        if(ok) { preferences_.transparency=percent; saveAppearance(); }
    });
    transparency->setObjectName("appearanceTransparency");
    appearance->addAction("Vordergrundfarbe …",this,[this] {
        const QColor color=QColorDialog::getColor(preferences_.foreground,this,"Farbe der Werte und Symbole");
        if(color.isValid()) { preferences_.foreground=color; saveAppearance(); }
    });
    appearance->addAction("Schriftart und Schriftschnitt …",this,[this] {
        bool ok=false;
        QFont font=QFontDialog::getFont(&ok,preferences_.valueFont,this,"Schrift der Messwerte");
        if(ok) { font.setPointSizeF(qBound(6.0,font.pointSizeF()>0 ? font.pointSizeF() : 10.0,48.0)); preferences_.valueFont=font; saveAppearance(); }
    });
    appearance->addAction("Schriftgröße …",this,[this] {
        bool ok=false;
        const int size=QInputDialog::getInt(this,"Schriftgröße","Größe in Punkt:",qRound(preferences_.valueFont.pointSizeF()),6,48,1,&ok);
        if(ok) { preferences_.valueFont.setPointSize(size); saveAppearance(); }
    });
    appearance->addSeparator();
    appearance->addAction("Darstellung zurücksetzen",this,[this] {
        const Preferences defaults;
        preferences_.background=defaults.background; preferences_.foreground=defaults.foreground;
        preferences_.transparency=defaults.transparency; preferences_.valueFont=defaults.valueFont;
        saveAppearance();
    });
    QMenu* values=menu.addMenu("Angezeigte Werte");
    for(int i=0;i<5;++i) {
        QAction* action=values->addAction(metricNames[i]); action->setCheckable(true);
        action->setChecked(preferences_.visibleValues.contains(metricKeys[i]));
        connect(action,&QAction::toggled,this,[this,i](bool on) {
            if(on) preferences_.visibleValues.append(metricKeys[i]); else preferences_.visibleValues.removeAll(metricKeys[i]);
            saveAppearance();
        });
    }
    menu.addSeparator();
    QAction* alarms=menu.addAction("Aktive Alarme …",this,&Desklet::showAlarms); alarms->setObjectName("showAlarms");
    QAction* acknowledge=menu.addAction("Alarme quittieren",this,&Desklet::acknowledgeAlarms);
    acknowledge->setObjectName("acknowledgeAlarms"); acknowledge->setEnabled(!activeAlerts_.isEmpty());
    QAction* alarmSettings=menu.addAction("Datenalter und Alarme …",this,&Desklet::showAlarmSettings);
    alarmSettings->setObjectName("alarmSettings");
    QAction* automationSettings=menu.addAction(QString("Lua-Automatik … [%1]")
        .arg(automationStateLabel(automationState_)),this,&Desklet::showAutomationSettings);
    automationSettings->setObjectName("automationSettingsAction");
    menu.addSeparator();
    QAction* refresh=menu.addAction("Statusverbindung neu starten (F5)",this,[this] { controller_.refresh(); });
    refresh->setEnabled(!controller_.busy() && !awaitingConfirmation_ && !demo_);
    QAction* settings=menu.addAction("Verbindung und Autostart …",this,&Desklet::showSettings);
    settings->setEnabled(!controller_.busy() && !awaitingConfirmation_ && !demo_);
    // Let the popup release its input grab before opening the focused dialog.
    menu.addAction("Diagnose / Gerätedaten (F1) …",this,[this] { QTimer::singleShot(0,this,&Desklet::showDetails); });
    if (!waylandSession_) menu.addAction("Position festlegen …",this,&Desklet::showPositionDialog);
    QAction* locked=menu.addAction("Position sperren"); locked->setCheckable(true); locked->setChecked(preferences_.locked);
    connect(locked,&QAction::toggled,this,[this](bool value) { preferences_.locked=value; rememberPosition(); });
    menu.addSeparator(); menu.addAction("Beenden",this,&QWidget::close); menu.exec(point);
}

void Desklet::closeEvent(QCloseEvent* event) {
    rememberPosition(); controller_.stop();
    if (tray_) tray_->hide();
    event->accept();
    if (!demo_) QCoreApplication::quit();
}
