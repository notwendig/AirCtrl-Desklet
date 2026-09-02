#include "desklet.hpp"
#include "diagnostics.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QFontDialog>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QPalette>
#include <QCursor>
#include <QScopedValueRollback>
#include <QWindow>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>
#include <time.h>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QClipboard>
#include <QDebug>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScreen>
#include <QShortcut>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
const QStringList metricKeys{"rh","rhset","temp","pm25","iaql"};
const QStringList metricNames{"Luftfeuchtigkeit","Zielfeuchte","Temperatur","PM2,5","IAI"};
QString metricText(const QString& key, const QJsonValue& value) {
    bool ok=value.isDouble();
    const double n=ok ? value.toDouble() : value.toString().toDouble(&ok);
    const bool humidity=key=="rh" || key=="rhset";
    ok=ok && std::isfinite(n) && n<=(humidity ? 100 : 999) && n>=(key=="temp" ? -100 : 0);
    const auto number=ok ? QString::number(n,'f',n==std::floor(n) ? 0 : 1) : QString("—");
    if(key=="rh") return "Feuchte " + number + (ok ? " %" : "");
    if(key=="rhset") return "Ziel " + number + (ok ? " %" : "");
    if(key=="temp") return number + " °C";
    if(key=="pm25") return "PM2,5 " + number + (ok ? " µg/m³" : "");
    return "IAI " + number;
}
bool powerKnown(const QJsonObject& state) {
    const auto v=state.value("pwr").toString(); return v=="0" || v=="1";
}
}
Desklet::Desklet(Preferences preferences, QString backend, bool demo)
    : preferences_(std::move(preferences)), controller_(std::move(backend), this), demo_(demo) {
    monitorClock_.start();
    preferences_.ageWarningSeconds=qBound(5,preferences_.ageWarningSeconds,3599);
    preferences_.ageStaleSeconds=qBound(preferences_.ageWarningSeconds+1,preferences_.ageStaleSeconds,7200);
    waylandSession_=QGuiApplication::platformName().startsWith("wayland") ||
        qEnvironmentVariable("XDG_SESSION_TYPE")=="wayland" ||
        (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariable("XDG_SESSION_TYPE")!="x11");
    setObjectName("desklet");
    setWindowTitle("Philips AirControl – Wohnzimmer");
    setWindowIcon(QIcon(":/airctrl-desklet.svg"));
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    installEventFilter(this);
    applyWindowMode();
    // Popup windows retain their own readable theme, even over a transparent desklet.
    setStyleSheet(R"(
        QMenu { background: #ffffff; color: #222222; font-size: 13px; padding: 4px; border: 1px solid #bdbdbd; }
        QMenu::item { padding: 7px 20px; }
        QMenu::item:selected { background: #e5e5e5; }
        QMenu::item:disabled { color: #777777; }
        QToolTip { color: #222222; background: #ffffed; border: 1px solid #bbbbbb; }
        QDialog { background: #ffffff; color: #222222; }
        QDialog QLabel, QDialog QCheckBox { color: #222222; }
        QDialog QPushButton { color: #222222; background: #eeeeee; border: 1px solid #bdbdbd; padding: 5px 9px; }
        QDialog QLineEdit, QDialog QSpinBox, QPlainTextEdit { color: #222222; background: #ffffff; padding: 5px; }
    )");
    auto* root=new QVBoxLayout(this); root->setContentsMargins(8,6,8,8); root->setSpacing(5);
    auto* bar=new QWidget(this); bar->setObjectName("controlBar");
    bar->installEventFilter(this);
    auto* row=new QHBoxLayout(bar); row->setContentsMargins(0,0,0,0); row->setSpacing(0);
    const QStringList names{"Ein / Aus","Kindersicherung","Automatikmodus","Lüfterstufe","Zielfeuchte","Beleuchtung","2-in-1-Modus","Timer"};
    const QStringList ids{"power","childLock","autoMode","fanSpeed","humidityTarget","light","function","timer"};
    for(int i=0;i<8;++i) {
        auto* b=new PanelButton(static_cast<PanelIcon>(i),names[i],bar); controls_[i]=b;
        b->setObjectName(ids[i]); b->setMinimumWidth(29); b->setFixedHeight(31); row->addWidget(b,1);
        b->installEventFilter(this);
        connect(b,&QPushButton::clicked,this,[this,i] { openControl(i); });
    }
    root->addWidget(bar);
    auto* statusArea=new QWidget(this); statusArea->setObjectName("statusArea");
    statusArea->installEventFilter(this);
    auto* statusRow=new QHBoxLayout(statusArea);
    statusRow->setContentsMargins(0,0,0,0); statusRow->setSpacing(6);
    emblemBar_=new QWidget(statusArea); emblemBar_->setObjectName("emblemBar");
    emblemBar_->installEventFilter(this);
    emblemLayout_=new QGridLayout(emblemBar_);
    emblemLayout_->setContentsMargins(0,0,0,0); emblemLayout_->setSpacing(4);
    emblemLayout_->setAlignment(Qt::AlignCenter);
    const QStringList emblemIds{"lock","mode","function","filter","water","clean","display","timer","wifi"};
    for(int i=0;i<emblemIds.size();++i) {
        auto* emblem=new Emblem(emblemBar_); emblems_[i]=emblem;
        emblem->setObjectName("emblem_"+emblemIds[i]);
        emblem->installEventFilter(this);
    }
    statusRow->addWidget(emblemBar_,1);
    monitorBar_=new MonitorBar(statusArea); monitorBar_->installEventFilter(this);
    statusRow->addWidget(monitorBar_,0,Qt::AlignVCenter); root->addWidget(statusArea);
    valueArea_=new QWidget(this); valueArea_->setObjectName("values");
    valueArea_->installEventFilter(this);
    valueLayout_=new QGridLayout(valueArea_); valueLayout_->setContentsMargins(0,0,0,0);
    valueLayout_->setHorizontalSpacing(14); valueLayout_->setVerticalSpacing(3);
    for(int i=0;i<5;++i) {
        auto* value=new QLabel(valueArea_); values_[i]=value;
        value->setObjectName("value_"+metricKeys[i]); value->setTextFormat(Qt::PlainText);
        value->setAlignment(Qt::AlignCenter); value->installEventFilter(this);
    }
    root->addWidget(valueArea_);
    auto* contextShortcut=new QShortcut(QKeySequence("Shift+F10"),this);
    connect(contextShortcut,&QShortcut::activated,this,[this] { openMenu(mapToGlobal(rect().center())); });
    auto* menuShortcut=new QShortcut(QKeySequence(Qt::Key_Menu),this);
    connect(menuShortcut,&QShortcut::activated,this,[this] { openMenu(mapToGlobal(rect().center())); });
    connect(&controller_,&Controller::statusReceived,this,&Desklet::applyStatus);
    connect(&controller_,&Controller::statusPacketReceived,this,[this] { recordReception(); updateMonitoring(); });
    connect(&controller_,&Controller::failed,this,&Desklet::setConnectionError);
    connect(&controller_,&Controller::commandFailed,this,[this](const QString& error) {
        awaitingConfirmation_=false; pending_={}; notice_=error; commandError_=error;
        activeCommandError_=error; ++commandFailureId_;
        qWarning().noquote()<<"AirControl – Schaltbefehl:"<<error;
        updateControls(); updateFooter();
    });
    connect(&controller_,&Controller::busyChanged,this,[this] { updateControls(); });
    connect(&controller_,&Controller::controlAccepted,this,[this] {
        notice_="Befehl angenommen · Rückmeldung wird gelesen …"; updateFooter();
    });
    auto* refresh=new QShortcut(QKeySequence("F5"),this);
    connect(refresh,&QShortcut::activated,this,[this] { if(!demo_ && !awaitingConfirmation_) controller_.refresh(); });
    auto* diagnostics=new QShortcut(QKeySequence("F1"),this);
    connect(diagnostics,&QShortcut::activated,this,&Desklet::showDetails);
    auto* timer=new QTimer(this); connect(timer,&QTimer::timeout,this,&Desklet::updateFooter); timer->start(1000);
    if(!demo_ && QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_=new QSystemTrayIcon(windowIcon(),this); tray_->setToolTip("Philips AirControl");
        auto* menu=new QMenu(this);
        menu->addAction("Widget anzeigen",this,[this] { showAndPosition(); });
        menu->addAction("Menü / Darstellung …",this,[this] { requestMenu(QCursor::pos()); });
        menu->addAction("Einstellungen",this,&Desklet::showSettings);
        menu->addAction("Diagnose",this,&Desklet::showDetails);
        menu->addAction("Beenden",this,&QWidget::close); tray_->setContextMenu(menu);
        connect(tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason) {
            if(reason==QSystemTrayIcon::Trigger) { show(); raise(); }
        });
        tray_->show();
    }
    applyAppearance(); updateControls(); updateFooter();
}
void Desklet::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(),Qt::transparent); p.setRenderHint(QPainter::Antialiasing);
    auto color=preferences_.background; color.setAlphaF((100-preferences_.transparency)/100.0);
    p.setPen(Qt::NoPen); p.setBrush(color); p.drawRoundedRect(rect(),5,5);
}
void Desklet::resizeToContent() {
    layout()->invalidate(); layout()->activate();
    setFixedSize(layout()->sizeHint().expandedTo(QSize(287,0)));
}
void Desklet::applyAppearance() {
    while(auto* item=valueLayout_->takeAt(0)) delete item;
    int visible=0;
    for(int i=0;i<5;++i) {
        auto* value=values_[i];
        value->setFont(preferences_.valueFont);
        const bool show=preferences_.visibleValues.contains(metricKeys[i]); value->setVisible(show);
        if(show) { valueLayout_->addWidget(value,visible/2,visible%2); ++visible; }
    }
    valueArea_->setVisible(visible>0);
    for(auto* button:controls_) { button->setForeground(preferences_.foreground); button->setFont(preferences_.valueFont); }
    monitorBar_->setFont(preferences_.valueFont);
    monitorBar_->setFixedSize(monitorBar_->sizeHint());
    updateEmblems(); updateValues(); updateMonitoring(); update();
}
void Desklet::saveAppearance() {
    applyAppearance();
    if(!demo_) preferences_.save();
    if(isVisible()) showAndPosition();
}
void Desklet::updateValues() {
    auto color=preferences_.foreground;
    if(!connected_ && updated_.isValid()) color.setAlpha(140);
    for(int i=0;i<5;++i) {
        auto* value=values_[i]; value->setText(metricText(metricKeys[i],status_.value(metricKeys[i])));
        auto palette=value->palette(); palette.setColor(QPalette::WindowText,color); value->setPalette(palette);
        value->setAccessibleName(metricNames[i]+": "+value->text());
    }
    resizeToContent();
}
void Desklet::updateEmblems() {
    // Reserve two rows of six emblems; alarms must not resize the whole window.
    const int side=qMax(24,QFontMetrics(preferences_.valueFont).height()+4);
    emblemBar_->setFixedHeight(2*side+4);
    emblemBar_->setMinimumWidth(6*side+5*4);
    while(auto* item=emblemLayout_->takeAt(0)) delete item;
    int index=0;
    const auto active=currentEmblems(status_,connected_);
    for(auto* emblem:emblems_) {
        const auto found=std::find_if(active.begin(),active.end(),[&](const EmblemState& state) {
            return emblem->objectName()=="emblem_"+state.id;
        });
        if(found!=active.end()) emblem->configure(*found,preferences_.foreground,preferences_.valueFont,
                                                connected_,updated_.isValid());
        emblem->setVisible(found!=active.end());
        if(found!=active.end()) { emblemLayout_->addWidget(emblem,index/6,index%6); ++index; }
    }
}
void Desklet::sendValues(const QJsonObject& values) {
    if(!connected_ || demo_ || awaitingConfirmation_ || controller_.busy()) return;
    pending_=values; awaitingConfirmation_=true; notice_="Änderung wird ausgeführt …";
    updateControls(); updateFooter(); controller_.setPanelValues(values);
}
void Desklet::openControl(int index) {
    if(!controls_[index]->isEnabled()) return;
    if(index==0) {
        if(demo_) { notice_="Vorschau – keine Gerätesteuerung"; updateFooter(); return; }
        if(awaitingConfirmation_ || controller_.busy()) {
            notice_="Ein Befehl läuft bereits · Geräterückmeldung abwarten …";
            updateFooter(); return;
        }
        const bool known=connected_ && powerKnown(status_);
        const bool turnOn=!known || status_["pwr"]!="1";
        pending_={{"pwr",turnOn ? "1" : "0"}};
        awaitingConfirmation_=true;
        notice_=known ? "Änderung wird ausgeführt …" : "Keine aktuelle Rückmeldung · Einschalten wird versucht …";
        updateControls(); updateFooter();
        controller_.setPower(turnOn);
        return;
    }
    if(index==1) { sendValues({{"cl",!status_["cl"].toBool()}}); return; }
    QMenu menu(this);
    auto item=[&](const QString& name,const QJsonObject& values) {
        auto* action=menu.addAction(name); action->setCheckable(true);
        bool selected=true;
        for(auto i=values.begin();i!=values.end();++i) if(status_.value(i.key())!=i.value()) selected=false;
        action->setChecked(selected);
        connect(action,&QAction::triggered,this,[this,values] { sendValues(values); });
    };
    if(index==2) {
        item("Automatik",{{"mode","P"}}); item("Allergen",{{"mode","A"}}); item("Nacht",{{"mode","S"},{"om","s"}});
    } else if(index==3) {
        for(const auto& s : {"1","2","3","t"}) item(QString(s)=="t" ? "Turbo" : "Stufe "+QString(s),{{"mode","M"},{"om",s}});
    } else if(index==4) {
        for(int n : {40,50,60,70}) item(QString::number(n)+" %",{{"rhset",n}});
    } else if(index==5) {
        if(status_.contains("aqil")) for(int n : {100,75,50,25,0}) item(n ? "Lichtring "+QString::number(n)+" %" : "Lichtring aus",{{"aqil",n}});
        if(status_.contains("uil")) { menu.addSeparator(); item("Geräteanzeige ein",{{"uil","1"}}); item("Geräteanzeige aus",{{"uil","0"}}); }
    } else if(index==6) {
        item("Nur Luftreinigung",{{"func","P"}}); item("Reinigung + Befeuchtung",{{"func","PH"}});
    } else if(index==7) {
        item("Timer aus",{{"dt",0}});
        for(int n=1;n<=12;++n) item(QString::number(n)+(n==1 ? " Stunde" : " Stunden"),{{"dt",n}});
    }
    menu.exec(controls_[index]->mapToGlobal(QPoint(0,controls_[index]->height())));
}
void Desklet::applyStatus(const QJsonObject& status) {
    status_=status; connected_=true; error_.clear(); updated_=QDateTime::currentDateTime();
    recordReception();
    setWindowTitle("Philips AirControl – "+status.value("name").toString("Luftreiniger"));
    notice_="Status empfangen";
    if(awaitingConfirmation_) {
        bool confirmed=true;
        for(auto i=pending_.begin();i!=pending_.end();++i) if(status.value(i.key())!=i.value()) confirmed=false;
        notice_=confirmed ? "Änderung vom Gerät bestätigt." : "Gerät meldet noch den bisherigen Wert.";
        if(confirmed) activeCommandError_.clear();
        else { activeCommandError_="Gerät hat den gewünschten Schaltzustand nicht bestätigt.";
            commandError_=activeCommandError_; ++commandFailureId_; }
    }
    awaitingConfirmation_=false; updateEmblems(); updateValues(); updateControls(); updateFooter();
}
void Desklet::setConnectionError(const QString& error) {
    connected_=false; error_=error; notice_=error;
    receptionFailed_=true;
    if(!controller_.busy()) awaitingConfirmation_=false;
    updateEmblems(); updateValues();
    qWarning().noquote()<<"AirControl:"<<error; updateControls(); updateFooter();
}
void Desklet::updateControls() {
    const bool ready=connected_ && !awaitingConfirmation_ && !controller_.busy() && !demo_;
    const bool unlocked=!status_["cl"].toBool();
    const bool on=status_["pwr"]=="1";
    const bool available[]={powerKnown(status_),status_["cl"].isBool(),status_.contains("mode"),
        status_.contains("mode") && status_.contains("om"),status_.contains("rhset"),
        status_.contains("aqil") || status_.contains("uil"),status_.contains("func"),status_.contains("dt")};
    for(int i=1;i<8;++i) controls_[i]->setEnabled(ready && available[i] && (i==1 || unlocked) && (i<=1 || on));
    auto* power=controls_[0];
    power->setEnabled(true);
    const bool known=connected_ && powerKnown(status_);
    power->setStatusColor(!known ? QColor("#ff9800") : on ? QColor("#2ecc71") : QColor("#ffffff"));
    QString powerTip=!connected_ ? "Keine Verbindung · Einschalten versuchen" :
        !known ? "Betriebszustand unbekannt · Einschalten versuchen" :
        on ? "Gerät an · Ausschalten" : "Gerät aus · Einschalten";
    if(demo_) powerTip+="\nVorschau – keine Gerätesteuerung";
    else if(awaitingConfirmation_) powerTip+="\nBefehl läuft · weitere Klicks senden keinen zusätzlichen Befehl";
    else if(!unlocked) powerTip+="\nKindersicherung aktiv · das Gerät kann den Befehl ablehnen";
    power->setToolTip(powerTip);
    power->setAccessibleDescription(powerTip);
    controls_[1]->setToolTip(status_["cl"].toBool() ? "Kindersicherung ausschalten" : "Kindersicherung einschalten");
    controls_[4]->setToolTip("Zielfeuchte: "+(status_.contains("rhset") ? QString::number(status_["rhset"].toInt())+" %" : "—"));
}
void Desklet::updateFooter() {
    QString age="Noch kein Empfang";
    if(lastDataAt_>=0) {
        age="Letzter Empfang vor "+QString::number(dataAgeSeconds())+" s";
    }
    const auto connection=demo_ ? QString("Vorschau – keine Gerätesteuerung") : connected_ ? QString("Verbunden") : QString("Keine Verbindung");
    const auto detail=connection+"\n"+preferences_.host+"\n"+age+"\n"+notice_+
        "\nKlick auf Wert oder Rechtsklick: Menü · Ziehen: Verschieben · F1: Diagnose";
    setToolTip(detail); setAccessibleDescription(detail);
    for(auto* value:values_) {
        value->setToolTip(detail); value->setAccessibleDescription(detail);
    }
    updateMonitoring();
}
qint64 Desklet::monotonicMs() const {
#ifdef CLOCK_BOOTTIME
    timespec time{};
    if(clock_gettime(CLOCK_BOOTTIME,&time)==0) return qint64(time.tv_sec)*1000+time.tv_nsec/1000000;
#endif
    return monitorClock_.elapsed();
}
qint64 Desklet::dataAgeSeconds() const {
    return lastDataAt_<0 ? -1 : qMax<qint64>(0,monotonicMs()-lastDataAt_)/1000;
}
void Desklet::recordReception() {
    lastDataAt_=monotonicMs(); packetReceivedAt_=QDateTime::currentDateTime(); receptionFailed_=false;
}
void Desklet::updateMonitoring() {
    const auto seconds=dataAgeSeconds();
    const auto freshness=dataFreshness(seconds,receptionFailed_,preferences_.ageWarningSeconds,preferences_.ageStaleSeconds);
    activeAlerts_=deviceAlerts(status_);
    if(receptionFailed_ || freshness==DataFreshness::Stale) {
        activeAlerts_.prepend({"reception",AlertLevel::Error,receptionFailed_ ?
            "Statusverbindung ausgefallen: "+error_ : "Daten zu alt: seit "+QString::number(seconds)+" s kein gültiges Statuspaket."});
    } else if(freshness==DataFreshness::Aging) {
        activeAlerts_.prepend({"reception",AlertLevel::Warning,
            "Achtung: seit "+QString::number(seconds)+" s kein gültiges Statuspaket."});
    }
    if(!activeCommandError_.isEmpty()) activeAlerts_.append({"command-"+QString::number(commandFailureId_),
        AlertLevel::Error,"Schaltfehler: "+activeCommandError_});
    bool acknowledged=!activeAlerts_.isEmpty();
    for(const auto& alert:activeAlerts_) acknowledged=acknowledged && alarmLatch_.acknowledged(alert);
    const auto state=freshness==DataFreshness::Waiting ? QString("Noch kein Datenempfang") :
        freshness==DataFreshness::Fresh ? QString("OK") : freshness==DataFreshness::Aging ? QString("Achtung") :
        freshness==DataFreshness::Stale ? QString("Daten zu alt") : QString("Verbindung ausgefallen");
    auto detail=state+" · Sekunden seit dem letzten gültigen Statuspaket\n"+
        QString("Grün: 0–%1 s · Gelb: %2–%3 s · Rot: ab %4 s oder Verbindungsfehler\n")
        .arg(preferences_.ageWarningSeconds-1).arg(preferences_.ageWarningSeconds)
        .arg(preferences_.ageStaleSeconds-1).arg(preferences_.ageStaleSeconds)+alertReport(activeAlerts_);
    if(!connected_ && updated_.isValid()) detail+="\nGerätewarnungen stammen aus dem letzten bestätigten Status.";
    if(acknowledged) detail+="\nAlarme quittiert (Q); ihre Ursache bleibt sichtbar.";
    if(alarmsPaused_) detail+="\nEmpfang für Verbindungseinstellungen pausiert; Benachrichtigungen ausgesetzt.";
    monitorBar_->setState(seconds,freshness,activeAlerts_,acknowledged,detail);
    if(alarmsPaused_) return;
    const auto fresh=alarmLatch_.update(activeAlerts_);
    if(!fresh.isEmpty()) {
        bool critical=false;
        for(const auto& alert:fresh) critical=critical || alert.level==AlertLevel::Error;
        const auto message=alertReport(fresh);
        emit alarmRaised(message,critical);
        if(!demo_) deliverAlarm(message,critical);
    }
}
void Desklet::deliverAlarm(const QString& message, bool critical) {
    if(preferences_.alarmSound) QApplication::beep();
    if(!preferences_.desktopAlarms) return;
    const auto request=alarmNotification(message,critical);
    auto* pending=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request),this);
    connect(pending,&QDBusPendingCallWatcher::finished,this,[this,pending] {
        const QDBusPendingReply<uint> reply=*pending;
        if(reply.isError() && !notificationFailureLogged_) {
            notificationFailureLogged_=true;
            qWarning().noquote()<<"AirControl: Desktop-Benachrichtigung nicht verfügbar:"<<reply.error().message();
        }
        pending->deleteLater();
    });
}
void Desklet::acknowledgeAlarms() {
    // A failed command is a past event; acknowledging it removes only its active
    // alarm. The last error remains in diagnostics. Ongoing conditions stay lit.
    alarmLatch_.acknowledge(activeAlerts_); activeCommandError_.clear(); updateMonitoring();
}
void Desklet::showAlarms() {
    QDialog dialog(this); dialog.setObjectName("alarmsDialog"); dialog.setWindowTitle("AirControl – aktive Alarme");
    dialog.resize(580,320); auto* layout=new QVBoxLayout(&dialog);
    auto* report=new QPlainTextEdit(&dialog); report->setObjectName("activeAlarmReport"); report->setReadOnly(true);
    layout->addWidget(report);
    const auto refresh=[&] {
        updateMonitoring();
        QString text=monitorBar_->accessibleDescription();
        if(report->toPlainText()!=text) report->setPlainText(text);
    };
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    auto* acknowledge=buttons->addButton("Quittieren",QDialogButtonBox::ActionRole);
    acknowledge->setObjectName("acknowledgeAlarms");
    connect(acknowledge,&QPushButton::clicked,&dialog,[&] { acknowledgeAlarms(); refresh(); });
    buttons->button(QDialogButtonBox::Close)->setText("Schließen");
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); layout->addWidget(buttons);
    QTimer timer; connect(&timer,&QTimer::timeout,&dialog,refresh); timer.start(1000); refresh(); dialog.exec();
}
void Desklet::showAlarmSettings() {
    QDialog dialog(this); dialog.setObjectName("alarmSettings"); dialog.setWindowTitle("Datenalter und Alarme");
    auto* layout=new QVBoxLayout(&dialog); auto* form=new QFormLayout;
    QSpinBox warning,stale; warning.setObjectName("ageWarningSeconds"); stale.setObjectName("ageStaleSeconds");
    warning.setRange(5,3599); warning.setSuffix(" s"); warning.setValue(preferences_.ageWarningSeconds);
    stale.setRange(warning.value()+1,7200); stale.setSuffix(" s"); stale.setValue(preferences_.ageStaleSeconds);
    connect(&warning,&QSpinBox::valueChanged,&stale,[&](int value) { stale.setMinimum(value+1); });
    form->addRow("Gelb / Achtung ab",&warning); form->addRow("Rot / zu alt ab",&stale); layout->addLayout(form);
    QCheckBox desktop("Desktop-Benachrichtigungen"),sound("Zusätzlicher Signalton");
    desktop.setObjectName("desktopAlarms"); sound.setObjectName("alarmSound");
    desktop.setChecked(preferences_.desktopAlarms); sound.setChecked(preferences_.alarmSound);
    layout->addWidget(&desktop); layout->addWidget(&sound);
    auto* note=new QLabel("Meldung einmal pro Alarm, erneut bei Verschärfung oder nach zwischenzeitlicher Behebung.\n"
        "Diese Grenzen ändern nicht den 90-s-Verbindungstimeout. Ohne Desktopdienst bleiben Alarme im Widget sichtbar.");
    note->setWordWrap(true); layout->addWidget(note);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
    buttons->button(QDialogButtonBox::Save)->setText("Speichern"); buttons->button(QDialogButtonBox::Cancel)->setText("Abbrechen");
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); layout->addWidget(buttons);
    if(dialog.exec()!=QDialog::Accepted) return;
    preferences_.ageWarningSeconds=warning.value(); preferences_.ageStaleSeconds=stale.value();
    preferences_.desktopAlarms=desktop.isChecked(); preferences_.alarmSound=sound.isChecked();
    if(!demo_) preferences_.save();
    updateMonitoring();
}
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
    controller_.configure(preferences_.host, preferences_.port, preferences_.interval);
    controller_.start();
}
void Desklet::showAndPosition() {
    adjustSize();
    if (waylandSession_) { show(); return; } // The compositor owns global placement.
    const QPoint desired = preferences_.position;
    QScreen* target = nullptr;
    for (auto* screen : QGuiApplication::screens()) {
        if (screen->availableGeometry().contains(desired)) { target = screen; break; }
    }
    if (!target) target = QGuiApplication::primaryScreen();
    if (target) {
        const auto available = target->availableGeometry();
        auto point = desired;
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
            const auto* context=static_cast<QContextMenuEvent*>(event);
            requestMenu(context->reason()==QContextMenuEvent::Keyboard ? mapToGlobal(rect().center()) : context->globalPos());
        }
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouse=static_cast<QMouseEvent*>(event);
        if (mouse->button()==Qt::RightButton) { rightPressed_=true; return true; }
        // Device buttons keep their left-click action. Only values and free
        // space provide click-to-menu and drag-to-move.
        if (mouse->button()==Qt::LeftButton && !qobject_cast<QAbstractButton*>(watched)) {
            leftPressed_=true; mouseMoved_=false;
            pressPosition_=mouse->globalPosition().toPoint(); dragOffset_=pressPosition_-pos();
            return true;
        }
    } else if (event->type()==QEvent::MouseMove && leftPressed_) {
        const auto global=static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
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
        const auto* mouse=static_cast<QMouseEvent*>(event);
        if (mouse->button()==Qt::RightButton) {
            rightPressed_=false; requestMenu(mouse->globalPosition().toPoint()); return true;
        }
        if (mouse->button()==Qt::LeftButton && leftPressed_) {
            leftPressed_=false;
            if (mouseMoved_) {
                if (!preferences_.locked && (preferences_.desktop || preferences_.hideDecoration)) rememberPosition();
            } else if(watched==monitorBar_) QTimer::singleShot(0,this,&Desklet::showAlarms);
            else requestMenu(mouse->globalPosition().toPoint());
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
    const auto connection=demo_ ? QString("Vorschau") : connected_ ? QString("Verbunden") : QString("Keine Verbindung");
    menu.addSection(status_.value("name").toString("AirControl")+" · "+connection);
    auto* decoration=menu.addAction("Fensterdekoration ausblenden");
    decoration->setObjectName("hideWindowDecoration"); decoration->setCheckable(true);
    decoration->setChecked(preferences_.hideDecoration);
    connect(decoration,&QAction::triggered,this,[this](bool hidden) {
        // Changing native flags hides/recreates the window. Do it after menu exec.
        QTimer::singleShot(0,this,[this,hidden] { setDecorationHidden(hidden); });
    });
    auto* appearance=menu.addMenu("Darstellung");
    appearance->addAction("Hintergrundfarbe …",this,[this] {
        const auto color=QColorDialog::getColor(preferences_.background,this,"Hintergrundfarbe");
        if(color.isValid()) { preferences_.background=color; saveAppearance(); }
    });
    auto* transparency=appearance->addAction("Hintergrundtransparenz …",this,[this] {
        bool ok=false;
        const auto percent=QInputDialog::getInt(this,"Hintergrundtransparenz", "Transparenz in % (0 = deckend, 100 = durchsichtig):",
                                               preferences_.transparency,0,100,5,&ok);
        if(ok) { preferences_.transparency=percent; saveAppearance(); }
    });
    transparency->setObjectName("appearanceTransparency");
    appearance->addAction("Vordergrundfarbe …",this,[this] {
        const auto color=QColorDialog::getColor(preferences_.foreground,this,"Farbe der Werte und Symbole");
        if(color.isValid()) { preferences_.foreground=color; saveAppearance(); }
    });
    appearance->addAction("Schriftart und Schriftschnitt …",this,[this] {
        bool ok=false;
        auto font=QFontDialog::getFont(&ok,preferences_.valueFont,this,"Schrift der Messwerte");
        if(ok) { font.setPointSizeF(qBound(6.0,font.pointSizeF()>0 ? font.pointSizeF() : 10.0,48.0)); preferences_.valueFont=font; saveAppearance(); }
    });
    appearance->addAction("Schriftgröße …",this,[this] {
        bool ok=false;
        const auto size=QInputDialog::getInt(this,"Schriftgröße","Größe in Punkt:",qRound(preferences_.valueFont.pointSizeF()),6,48,1,&ok);
        if(ok) { preferences_.valueFont.setPointSize(size); saveAppearance(); }
    });
    appearance->addSeparator();
    appearance->addAction("Darstellung zurücksetzen",this,[this] {
        const Preferences defaults;
        preferences_.background=defaults.background; preferences_.foreground=defaults.foreground;
        preferences_.transparency=defaults.transparency; preferences_.valueFont=defaults.valueFont;
        saveAppearance();
    });
    auto* values=menu.addMenu("Angezeigte Werte");
    for(int i=0;i<5;++i) {
        auto* action=values->addAction(metricNames[i]); action->setCheckable(true);
        action->setChecked(preferences_.visibleValues.contains(metricKeys[i]));
        connect(action,&QAction::toggled,this,[this,i](bool on) {
            if(on) preferences_.visibleValues.append(metricKeys[i]); else preferences_.visibleValues.removeAll(metricKeys[i]);
            saveAppearance();
        });
    }
    menu.addSeparator();
    auto* alarms=menu.addAction("Aktive Alarme …",this,&Desklet::showAlarms); alarms->setObjectName("showAlarms");
    auto* acknowledge=menu.addAction("Alarme quittieren",this,&Desklet::acknowledgeAlarms);
    acknowledge->setObjectName("acknowledgeAlarms"); acknowledge->setEnabled(!activeAlerts_.isEmpty());
    auto* alarmSettings=menu.addAction("Datenalter und Alarme …",this,&Desklet::showAlarmSettings);
    alarmSettings->setObjectName("alarmSettings");
    menu.addSeparator();
    auto* refresh=menu.addAction("Statusverbindung neu starten (F5)",this,[this] { controller_.refresh(); });
    refresh->setEnabled(!controller_.busy() && !awaitingConfirmation_ && !demo_);
    auto* settings=menu.addAction("Verbindung und Autostart …",this,&Desklet::showSettings);
    settings->setEnabled(!controller_.busy() && !awaitingConfirmation_ && !demo_);
    menu.addAction("Diagnose / Gerätedaten (F1) …",this,&Desklet::showDetails);
    if (!waylandSession_) menu.addAction("Position festlegen …",this,&Desklet::showPositionDialog);
    auto* locked=menu.addAction("Position sperren"); locked->setCheckable(true); locked->setChecked(preferences_.locked);
    connect(locked,&QAction::toggled,this,[this](bool value) { preferences_.locked=value; rememberPosition(); });
    menu.addSeparator(); menu.addAction("Beenden",this,&QWidget::close); menu.exec(point);
}
void Desklet::showPositionDialog() {
    if (waylandSession_) return;
    QDialog dialog(this); dialog.setWindowTitle("Widget-Position"); dialog.setObjectName("positionDialog");
    auto* layout=new QVBoxLayout(&dialog); auto* form=new QFormLayout;
    QSpinBox horizontal, vertical;
    horizontal.setObjectName("positionX"); vertical.setObjectName("positionY");
    horizontal.setRange(-32768,32767); vertical.setRange(-32768,32767);
    horizontal.setValue(x()); vertical.setValue(y());
    form->addRow("Horizontal (X):",&horizontal); form->addRow("Vertikal (Y):",&vertical); layout->addLayout(form);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Übernehmen");
    buttons->button(QDialogButtonBox::Cancel)->setText("Abbrechen"); layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if (dialog.exec()==QDialog::Accepted) {
        move(horizontal.value(),vertical.value()); rememberPosition();
    }
}
void Desklet::showSettings() {
    if (controller_.busy() || awaitingConfirmation_ || demo_) return;
    QScopedValueRollback<bool> paused(alarmsPaused_,true);
    controller_.stop(); // no old-host reply can arrive during a modal configuration change
    QDialog dialog(this); dialog.setWindowTitle("AirControl – Einstellungen");
    auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
    QLineEdit host(preferences_.host); host.setMinimumWidth(240);
    QSpinBox port; port.setRange(1,65535); port.setValue(preferences_.port);
    QSpinBox interval; interval.setRange(5,300); interval.setSuffix(" Sekunden"); interval.setValue(preferences_.interval);
    QCheckBox desktop("Desktopmodus (unter X11 hinter normalen Fenstern)"); desktop.setChecked(preferences_.desktop);
    desktop.setToolTip("Die Fensterdekoration wird separat im Kontextmenü ein- oder ausgeblendet.");
    QCheckBox autostart("Bei der Anmeldung starten"); autostart.setChecked(QFileInfo::exists(autostartPath()));
    interval.setToolTip("Pause vor einem neuen Verbindungsversuch nach einem Fehler. Statusmeldungen kommen automatisch vom Gerät.");
    form->addRow("Geräteadresse", &host); form->addRow("UDP-Port", &port); form->addRow("Wiederverbindung nach Fehler", &interval);
    layout->addLayout(form); layout->addWidget(&desktop); layout->addWidget(&autostart);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("Speichern"); buttons->button(QDialogButtonBox::Cancel)->setText("Abbrechen");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (host.text().trimmed().isEmpty()) { host.setFocus(); return; }
        QString error;
        if (!setAutostart(autostart.isChecked(), &error)) { QMessageBox::warning(&dialog, "Autostart", error); return; }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) { controller_.start(); return; }
    const bool changedDevice = preferences_.host != host.text().trimmed() || preferences_.port != port.value();
    const bool changedMode = preferences_.desktop != desktop.isChecked();
    preferences_.host = host.text().trimmed(); preferences_.port = port.value(); preferences_.interval = interval.value();
    preferences_.desktop = desktop.isChecked(); rememberPosition();
    if (changedMode) { applyWindowMode(); showAndPosition(); }
    if (changedDevice) {
        status_={}; updated_={}; packetReceivedAt_={}; connected_=false; error_.clear();
        lastDataAt_=-1; receptionFailed_=false; activeCommandError_.clear(); commandError_.clear();
        activeAlerts_.clear(); alarmLatch_={}; notice_="Gerät gewechselt";
        updateEmblems(); updateValues(); updateControls(); updateFooter();
    }
    controller_.configure(preferences_.host, preferences_.port, preferences_.interval);
    controller_.start();
}
void Desklet::showDetails() {
    QDialog dialog(this); dialog.setWindowTitle("AirControl – Diagnose"); dialog.resize(860,620);
    dialog.setMinimumSize(640,420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget(&dialog); tabs->setObjectName("diagnosticTabs");
    const auto heading = QString("AirControl %1\nGerät: %2:%3\nPlattform: %4\nBackend: %5\n"
                                 "Empfang: dauerhafte CoAP-Beobachtung (status-observe)\n"
                                 "Anlauf: 60 s je Anfrage · Datenpause: 90 s · Schaltanfrage: 10 s\nLetzter Empfang: %6\n\n")
        .arg(QCoreApplication::applicationVersion(), preferences_.host)
        .arg(preferences_.port).arg(QGuiApplication::platformName(), controller_.backendPath(),
            updated_.isValid() ? updated_.toString(Qt::ISODate) : "noch keiner");
    const auto session=QString("Desktopsitzung: %1\nWayland-Behandlung: %2\n\n")
        .arg(qEnvironmentVariable("XDG_SESSION_TYPE","unbekannt"),waylandSession_ ? "ja" : "nein");
    const auto errorText=(error_.isEmpty() ? QString() : "Letzter Verbindungsfehler:\n"+error_+"\n\n")+
        (commandError_.isEmpty() ? QString() : "Letzter Schaltfehler:\n"+commandError_+"\n\n");
    const auto rawJson=QString::fromUtf8(QJsonDocument(status_).toJson(QJsonDocument::Indented));
    const auto deviceFields=describeDeviceFields(status_);
    const auto table=[&](const QList<DiagnosticField>& fields,const QString& name,bool hex=false) {
        const int descriptionColumn=hex ? 3 : 2;
        auto* result=new QTableWidget(fields.size(),descriptionColumn+1,&dialog); result->setObjectName(name);
        result->setHorizontalHeaderLabels(hex ? QStringList{"Tag","Empfangener Wert","Code (Hex)","Bedeutung"}
                                             : QStringList{"Tag","Empfangener Wert","Bedeutung"});
        result->setAlternatingRowColors(true); result->setWordWrap(true);
        result->setEditTriggers(QAbstractItemView::NoEditTriggers);
        result->setSelectionBehavior(QAbstractItemView::SelectRows);
        result->setSelectionMode(QAbstractItemView::SingleSelection);
        result->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        result->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
        result->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        if(hex) result->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
        result->horizontalHeader()->setSectionResizeMode(descriptionColumn,QHeaderView::Stretch);
        const auto fixed=QFontDatabase::systemFont(QFontDatabase::FixedFont);
        for(int row=0;row<fields.size();++row) {
            auto* tag=new QTableWidgetItem(fields[row].tag); tag->setFont(fixed);
            auto* value=new QTableWidgetItem(fields[row].value); value->setFont(fixed);
            auto* description=new QTableWidgetItem(fields[row].description);
            tag->setToolTip(fields[row].tag); value->setToolTip(fields[row].value);
            description->setToolTip(fields[row].description);
            result->setItem(row,0,tag); result->setItem(row,1,value); result->setItem(row,descriptionColumn,description);
            if(hex) {
                auto* code=new QTableWidgetItem(fields[row].hex); code->setFont(fixed);
                code->setToolTip(fields[row].hex.isEmpty() ? "Kein numerischer Fehler-/Statuscode." : "Hexadezimale Darstellung: "+fields[row].hex);
                result->setItem(row,2,code);
            }
        }
        result->setAccessibleName("Diagnosefelder mit Tag, Rohwert und deutscher Bedeutung");
        return result;
    };
    tabs->addTab(table(deviceFields,"deviceFields",true),"Gerätewerte erklärt");
    QList<DiagnosticField> connectionFields{
        {"AirControl",QCoreApplication::applicationVersion(),"Version des Qt-Widgets."},
        {"Gerät",preferences_.host+":"+QString::number(preferences_.port),"Konfigurierte Zieladresse und UDP-Port des Luftreinigers."},
        {"Verbindung",demo_ ? "Vorschau" : connected_ ? "Verbunden" : "Keine Verbindung","Zustand der Verbindung aus Sicht des Widgets."},
        {"Plattform",QGuiApplication::platformName(),"Tatsächlich von Qt verwendetes Fenster-Backend, z.B. xcb oder wayland."},
        {"Desktopsitzung",qEnvironmentVariable("XDG_SESSION_TYPE","unbekannt"),"Vom Desktop gemeldeter Sitzungstyp. Er kann vom Qt-Fenster-Backend abweichen."},
        {"Wayland-Behandlung",waylandSession_ ? "ja" : "nein","Ob das Widget seine Wayland-spezifische Fensterbehandlung verwendet."},
        {"Backend",controller_.backendPath(),"Pfad des separaten C++-Programms für die verschlüsselte CoAP-Kommunikation."},
        {"Empfangsmodus","Dauerhafte Beobachtung","Ein langlebiger status-observe-Prozess empfängt neue Meldungen ohne regelmäßige Einzelabfragen."},
        {"Empfangsphase",controller_.observationProgress(),"Fortschritt des Empfängers; der Hintergrundempfang sperrt keine Bedienelemente."},
        {"Statusmeldungen",QString::number(controller_.statusCount()),"Anzahl gültiger Statuszeilen seit Programmstart."},
        {"Datenalter",dataAgeSeconds()<0 ? "noch kein Status" : QString::number(dataAgeSeconds())+" s","Seit dem letzten gültigen Statuspaket; auch Pakete während eines Schreibbefehls zählen. Keine Rücksetzung durch Fehler oder Schalt-ACKs."},
        {"Letztes Statuspaket",packetReceivedAt_.isValid() ? packetReceivedAt_.toString(Qt::ISODate) : "noch keines","Empfangszeit des letzten gültigen Pakets, unabhängig von der Bestätigung eines Schaltbefehls."},
        {"Datenalter-Grenzen",QString("Gelb ab %1 s; Rot ab %2 s").arg(preferences_.ageWarningSeconds).arg(preferences_.ageStaleSeconds),"Lokale Alarmgrenzen. Verbindungsfehler sind sofort rot. Der CoAP-Timeout wird dadurch nicht verändert."},
        {"Aktive Alarme",alertReport(activeAlerts_),"Warnungen aus bekannten Gerätestatusfeldern sowie Fehler beim Empfang oder Schalten. Unbekannte err-Codes werden nicht geraten."},
        {"Beobachtungsstarts",QString::number(controller_.observationStarts()),"Ein Start im Normalbetrieb; weitere Starts nur nach Fehler, F5 oder geänderten Einstellungen."},
        {"CoAP-Anlauf","60 Sekunden je Anfrage","Synchronisierung und erste Statusantwort erhalten jeweils bis zu 60 Sekunden. Der Prozess-Watchdog erlaubt insgesamt 125 Sekunden."},
        {"Maximale Datenpause","90 Sekunden","Erst nach längerem Ausbleiben von Statusmeldungen wird neu verbunden. Die beobachteten 18–19 Sekunden sind normale Pausen."},
        {"Schaltbefehl","10 Sekunden je Anfrage","Separater Schreibprozess; maximal 25 Sekunden einschließlich Synchronisierung. Keine automatische Wiederholung."},
        {"Statusbestätigung","90 Sekunden","Nach der Schreibannahme wird auf eine neue Meldung der laufenden Beobachtung gewartet."},
        {"Wiederverbindung",QString::number(preferences_.interval)+" Sekunden","Pause vor einem neuen Empfangsversuch nach einem Fehler; kein Abfrageintervall."},
        {"Letzter Empfang",updated_.isValid() ? updated_.toString(Qt::ISODate) : "noch keiner","Zeitpunkt des letzten in Werte und Embleme übernommenen Status; während eines Schreibbefehls kann ein jüngeres Paket vorliegen."}
    };
    if(!error_.isEmpty()) connectionFields.append({"Letzter Fehler",error_,"Unveränderte letzte Fehlermeldung des Backends bzw. der Verbindungssteuerung."});
    if(!commandError_.isEmpty()) connectionFields.append({"Letzter Schaltfehler",commandError_,"Ein Schaltfehler bedeutet nicht automatisch, dass die weiterhin aktive Beobachtung offline ist."});
    tabs->addTab(table(connectionFields,"connectionFields"),"Verbindung erklärt");
    auto* raw = new QPlainTextEdit(&dialog); raw->setObjectName("rawDiagnostics"); raw->setReadOnly(true);
    raw->setAccessibleName("Unveränderte Verbindungsdiagnose und Geräte-Rohdaten");
    raw->setPlainText(heading+session+errorText+rawJson); tabs->addTab(raw,"Rohdaten");
    layout->addWidget(tabs);
    auto* note=new QLabel("Code (Hex): Fehler-/Statuscodes zusätzlich hexadezimal. Rohwerte bleiben unverändert; unbekannte Codes werden nicht als gesicherter Wartungsalarm bewertet.",&dialog);
    note->setWordWrap(true); layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* copy = buttons->addButton("Bericht kopieren", QDialogButtonBox::ActionRole);
    connect(copy,&QPushButton::clicked,this,[heading,session,errorText,deviceFields,connectionFields,rawJson] {
        QApplication::clipboard()->setText(heading+session+errorText+"ERKLÄRTE VERBINDUNGSFELDER\n"+
            diagnosticFieldReport(connectionFields)+"\nERKLÄRTE GERÄTEWERTE\n"+diagnosticFieldReport(deviceFields)+
            "\nUNVERÄNDERTE ROHDATEN\n"+rawJson);
    });
    buttons->button(QDialogButtonBox::Close)->setText("Schließen");
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); layout->addWidget(buttons); dialog.exec();
}
void Desklet::closeEvent(QCloseEvent* event) {
    rememberPosition(); controller_.stop();
    if (tray_) tray_->hide();
    event->accept();
    if (!demo_) QCoreApplication::quit();
}
