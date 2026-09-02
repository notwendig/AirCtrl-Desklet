#include "desklet.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <QMenu>
#include <QInputDialog>
#include <QContextMenuEvent>
#include <QSettings>
#include <QMouseEvent>
#include <QSpinBox>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QClipboard>

class ScopedEnvironment {
public:
    ScopedEnvironment(const char* name, const QByteArray& value)
        : name_(name), old_(qgetenv(name)), existed_(qEnvironmentVariableIsSet(name)) { qputenv(name,value); }
    ~ScopedEnvironment() { if(existed_) qputenv(name_,old_); else qunsetenv(name_); }
private:
    const char* name_;
    QByteArray old_;
    bool existed_;
};

class NativeMoveProbe : public Desklet {
public:
    using Desklet::Desklet;
    int moveRequests=0;
protected:
    bool startNativeMove() override { ++moveRequests; return true; }
};

class EnablementRecorder : public QObject {
public:
    int disabled=0;
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type()==QEvent::EnabledChange && !static_cast<QWidget*>(watched)->isEnabled()) ++disabled;
        return false;
    }
};

class Tests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir temp_;
    QString log_, state_;
    bool releaseRead() {
        QFile gate(temp_.filePath("read.ready"));
        return gate.open(QIODevice::WriteOnly);
    }
    QList<QJsonArray> calls() {
        QFile file(log_); file.open(QIODevice::ReadOnly);
        QList<QJsonArray> out;
        for (const auto& line : file.readAll().split('\n'))
            if (!line.isEmpty()) out.append(QJsonDocument::fromJson(line).array());
        return out;
    }
    int clickMenus(QWidget* target, Qt::MouseButton button, bool nativeContext=false) {
        int opened=0;
        QTimer closer;
        connect(&closer,&QTimer::timeout,this,[&] {
            if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                if(menu->objectName()=="deskletContextMenu") { ++opened; menu->close(); }
            }
        });
        closer.start(25);
        QTest::mouseClick(target,button,Qt::NoModifier,QPoint(3,3));
        if(nativeContext) {
            QContextMenuEvent event(QContextMenuEvent::Mouse,QPoint(3,3),target->mapToGlobal(QPoint(3,3)));
            QApplication::sendEvent(target,&event);
        }
        QTest::qWait(90);
        return opened;
    }
private slots:
    void initTestCase() {
        QVERIFY(temp_.isValid());
        QCoreApplication::setOrganizationName("AirControlTests");
        QCoreApplication::setApplicationName("AirControlTests");
        qputenv("XDG_CONFIG_HOME", temp_.path().toUtf8());
        log_ = temp_.filePath("calls.jsonl"); state_ = temp_.filePath("state.json");
        qputenv("AIRCTRL_TEST_LOG", log_.toUtf8()); qputenv("AIRCTRL_TEST_STATE", state_.toUtf8());
    }
    void init() {
        qputenv("AIRCTRL_TEST_MODE", ""); QFile::remove(log_); QFile::remove(state_);
        qunsetenv("AIRCTRL_TEST_READ_GATE"); QFile::remove(temp_.filePath("read.ready"));
        QSettings().clear();
    }
    void statusNoWrite() {
        Controller c(FAKE_BACKEND); QSignalSpy status(&c, &Controller::statusReceived);
        c.configure("127.0.0.1", 12345, 5); c.start();
        QTRY_COMPARE(status.count(), 1);
        QCOMPARE(status.at(0).at(0).toJsonObject().value("rh").toInt(), 55);
        QCOMPARE(calls().size(), 1);
        QVERIFY(calls()[0].contains("status")); QVERIFY(!calls()[0].contains("set"));
        const auto args = calls()[0];
        const auto timeoutIndex = args.toVariantList().indexOf("--timeout");
        QVERIFY(timeoutIndex >= 0);
        QCOMPARE(args[timeoutIndex + 1].toString(), QString("10"));
        c.stop();
    }
    void typedWritesAndReadback() {
        Controller c(FAKE_BACKEND); QSignalSpy status(&c, &Controller::statusReceived);
        QSignalSpy accepted(&c, &Controller::controlAccepted);
        c.start(); QTRY_COMPARE(status.count(), 1);
        c.setPower(false); QTRY_COMPARE(accepted.count(), 1); QTRY_COMPARE(status.count(), 2);
        QCOMPARE(status.last()[0].toJsonObject()["pwr"].toString(), QString("0"));
        c.setHumidity(60); QTRY_COMPARE(accepted.count(), 2); QTRY_COMPARE(status.count(), 3);
        QCOMPARE(status.last()[0].toJsonObject()["rhset"].toInt(), 60);
        const auto requests = calls();
        QCOMPARE(requests.size(), 5);
        QVERIFY(requests[1].contains("pwr=0")); QVERIFY(!requests[1].contains("-I"));
        QVERIFY(requests[3].contains("rhset=60")); QVERIFY(requests[3].contains("-I"));
        QVERIFY(!requests[3].contains("rh=60"));
        c.stop();
    }
    void pollingKeepsButtonsEnabledAndClickIsExecutedOnce() {
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition();
        auto* controller=widget.findChild<Controller*>(); QVERIFY(controller);
        QSignalSpy status(controller,&Controller::statusReceived), accepted(controller,&Controller::controlAccepted);
        widget.start(); QTRY_COMPARE(status.count(),1);
        const auto buttons=widget.findChildren<QPushButton*>(); QCOMPARE(buttons.size(),8);
        EnablementRecorder recorder;
        for(auto* button:buttons) { QVERIFY(button->isEnabled()); button->installEventFilter(&recorder); }

        qputenv("AIRCTRL_TEST_READ_GATE",temp_.filePath("read.ready").toUtf8());
        controller->refresh(); QTRY_COMPARE(calls().size(),2); QVERIFY(controller->busy());
        for(auto* button:buttons) QVERIFY(button->isEnabled());
        QCOMPARE(recorder.disabled,0);
        QVERIFY(releaseRead()); QTRY_COMPARE(status.count(),2);
        QCOMPARE(recorder.disabled,0); // includes all transitions throughout the poll

        QVERIFY(QFile::remove(temp_.filePath("read.ready")));
        controller->refresh(); QTRY_COMPARE(calls().size(),3); QVERIFY(controller->busy());
        auto* power=widget.findChild<QPushButton*>("power"); QVERIFY(power->isEnabled());
        QCOMPARE(recorder.disabled,0);
        QTest::mouseClick(power,Qt::LeftButton);
        for(auto* button:buttons) QVERIFY(!button->isEnabled());
        QTest::mouseClick(power,Qt::LeftButton); // a second click cannot queue another write
        QTest::qWait(60);
        QCOMPARE(calls().size(),3); QCOMPARE(accepted.count(),0); QCOMPARE(status.count(),2);
        QVERIFY(releaseRead()); QTRY_COMPARE(accepted.count(),1);
        QCOMPARE(status.count(),2); // the older polling reply must not confirm the write
        QVERIFY(!power->isEnabled());
        QTRY_COMPARE(status.count(),3);
        QVERIFY(power->isEnabled()); QCOMPARE(power->toolTip(),QString("Einschalten"));
        QCOMPARE(status.last()[0].toJsonObject()["pwr"].toString(),QString("0"));
        const auto requests=calls(); QCOMPARE(requests.size(),5);
        for(int i=0;i<3;++i) QVERIFY(requests[i].contains("status"));
        QVERIFY(requests[3].contains("set")); QVERIFY(requests[3].contains("pwr=0"));
        QVERIFY(requests[4].contains("status"));
        controller->stop();
    }
    void readErrorCancelsQueuedWrite_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("process-error") << QString("failure");
        QTest::newRow("bad-json") << QString("bad-json");
        QTest::newRow("timeout") << QString("timeout");
    }
    void readErrorCancelsQueuedWrite() {
        QFETCH(QString,mode);
        qputenv("AIRCTRL_TEST_MODE",mode.toUtf8());
        qputenv("AIRCTRL_TEST_READ_GATE",temp_.filePath("read.ready").toUtf8());
        Controller c(FAKE_BACKEND); if(mode=="timeout") c.setWatchdogInterval(300);
        QSignalSpy errors(&c,&Controller::failed), status(&c,&Controller::statusReceived), accepted(&c,&Controller::controlAccepted);
        c.start(); QTRY_COMPARE(calls().size(),1); QVERIFY(c.busy());
        c.setPower(false);
        if(mode!="timeout") QVERIFY(releaseRead());
        QTRY_COMPARE(errors.count(),1); QCOMPARE(accepted.count(),0); QCOMPARE(status.count(),0);
        QCOMPARE(calls().size(),1);
        qputenv("AIRCTRL_TEST_MODE",""); qunsetenv("AIRCTRL_TEST_READ_GATE");
        c.refresh(); QTRY_COMPARE(status.count(),1);
        QCOMPARE(calls().size(),2); QCOMPARE(accepted.count(),0);
        QVERIFY(calls()[1].contains("status"));
        QCOMPARE(status.last()[0].toJsonObject()["pwr"].toString(),QString("1"));
        c.stop();
    }
    void stopCancelsQueuedWrite() {
        qputenv("AIRCTRL_TEST_READ_GATE",temp_.filePath("read.ready").toUtf8());
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived), accepted(&c,&Controller::controlAccepted);
        c.start(); QTRY_COMPARE(calls().size(),1); c.setPower(false); c.stop();
        QTRY_VERIFY(!c.busy());
        qunsetenv("AIRCTRL_TEST_READ_GATE"); c.start(); QTRY_COMPARE(status.count(),1);
        QCOMPARE(calls().size(),2); QCOMPARE(accepted.count(),0);
        QVERIFY(calls()[1].contains("status"));
        QCOMPARE(status.last()[0].toJsonObject()["pwr"].toString(),QString("1"));
        c.stop();
    }
    void rejectUnsupportedHumidity() {
        Controller c(FAKE_BACKEND); QSignalSpy errors(&c, &Controller::failed);
        c.setHumidity(55); QCOMPARE(errors.count(), 1); QVERIFY(!QFile::exists(log_));
    }
    void processFailureAndRecovery() {
        qputenv("AIRCTRL_TEST_MODE", "failure");
        Controller c(FAKE_BACKEND); QSignalSpy errors(&c, &Controller::failed);
        QSignalSpy status(&c, &Controller::statusReceived);
        c.start(); QTRY_COMPARE(errors.count(), 1); QVERIFY(!c.busy());
        qputenv("AIRCTRL_TEST_MODE", ""); c.refresh(); QTRY_COMPARE(status.count(), 1); c.stop();
    }
    void badJson() {
        qputenv("AIRCTRL_TEST_MODE", "bad-json");
        Controller c(FAKE_BACKEND); QSignalSpy errors(&c, &Controller::failed);
        QSignalSpy status(&c, &Controller::statusReceived);
        c.start(); QTRY_COMPARE(errors.count(), 1); QCOMPARE(status.count(), 0); c.stop();
    }
    void automaticallyRetryRead() {
        qputenv("AIRCTRL_TEST_MODE", "failure-once");
        Controller c(FAKE_BACKEND);
        QSignalSpy errors(&c, &Controller::failed), status(&c, &Controller::statusReceived);
        c.start(); QTRY_COMPARE(errors.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(status.count(), 1, 2500);
        QCOMPARE(calls().size(), 2);
        c.stop();
    }
    void neverRetryWrite() {
        qputenv("AIRCTRL_TEST_MODE", "write-failure");
        Controller c(FAKE_BACKEND);
        QSignalSpy errors(&c, &Controller::failed), status(&c, &Controller::statusReceived);
        c.start(); QTRY_COMPARE(status.count(), 1);
        c.setPower(false); QTRY_COMPARE(errors.count(), 1);
        QTest::qWait(1300);
        QCOMPARE(calls().size(), 2); // one read and exactly one write
        c.stop();
    }
    void stopCancelsReadRetry() {
        qputenv("AIRCTRL_TEST_MODE", "failure");
        Controller c(FAKE_BACKEND);
        QSignalSpy errors(&c, &Controller::failed);
        c.start(); QTRY_COMPARE(errors.count(), 1); c.stop();
        QTest::qWait(1300); QCOMPARE(calls().size(), 1);
    }
    void timeoutNoOverlap() {
        qputenv("AIRCTRL_TEST_MODE", "timeout");
        Controller c(FAKE_BACKEND); c.setWatchdogInterval(200);
        QSignalSpy errors(&c, &Controller::failed);
        c.start(); c.refresh(); c.refresh(); QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(calls().size(), 1); QVERIFY(!c.busy()); c.stop();
    }
    void missingBackend() {
        Controller c(temp_.filePath("missing")); QSignalSpy errors(&c, &Controller::failed);
        c.start(); QCOMPARE(errors.count(), 1); QVERIFY(!c.busy()); c.stop();
    }
    void widgetMappingAndOffline() {
        Preferences p; p.desktop = false;
        Desklet widget(p, FAKE_BACKEND);
        widget.applyStatus({{"name","Wohnzimmer"},{"pwr","1"},{"rh",55},{"rhset",50},{"temp",24},{"pm25",1},{"iaql",2}});
        QCOMPARE(widget.findChild<QLabel*>("value_rh")->text(),QString("Feuchte 55 %"));
        QCOMPARE(widget.findChild<QLabel*>("value_temp")->text(),QString("24 °C"));
        QCOMPARE(widget.findChild<QLabel*>("value_pm25")->text(),QString("PM2,5 1 µg/m³"));
        auto* target=widget.findChild<QPushButton*>("humidityTarget");
        QVERIFY(target->toolTip().contains("50 %"));
        QVERIFY(widget.findChild<QPushButton*>("power")->isEnabled());
        QVERIFY(!QFile::exists(log_));
        widget.setConnectionError("offline");
        QVERIFY(!widget.findChild<QPushButton*>("power")->isEnabled());
        QVERIFY(!target->isEnabled());
        QVERIFY(widget.toolTip().contains("Keine Verbindung"));
        QVERIFY(widget.toolTip().contains("offline"));
        auto* value=widget.findChild<QLabel*>("value_rh");
        QCOMPARE(value->text(),QString("Feuchte 55 %"));
        QVERIFY(value->accessibleDescription().contains("Letzter Empfang"));
        QVERIFY(value->palette().color(QPalette::WindowText).alpha()<255);
        widget.applyStatus({{"name","other"}});
        QCOMPARE(value->text(),QString("Feuchte —"));
        QVERIFY(!widget.findChild<QPushButton*>("power")->isEnabled());
        QVERIFY(!target->isEnabled());
    }
    void diagnosticWindowExplainsFieldsAndPreservesRawData() {
        Desklet widget(Preferences{},FAKE_BACKEND,true);
        widget.applyStatus({{"pwr","1"},{"cl",false},{"pm25",5},{"err",49236},
            {"DeviceId","test-device"},{"future_tag",QJsonObject{{"x",1}}}});
        bool inspected=false, screenshotSaved=false;
        int deviceRows=-1, connectionRows=-1;
        QString powerDescription,errorDescription,unknownDescription,unknownValue,rawText,copied;
        QTimer::singleShot(80,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog) return;
            auto* device=dialog->findChild<QTableWidget*>("deviceFields");
            auto* connection=dialog->findChild<QTableWidget*>("connectionFields");
            auto* raw=dialog->findChild<QPlainTextEdit*>("rawDiagnostics");
            if(device && connection && raw) {
                deviceRows=device->rowCount(); connectionRows=connection->rowCount(); rawText=raw->toPlainText();
                for(int row=0;row<device->rowCount();++row) {
                    const auto tag=device->item(row,0)->text();
                    if(tag=="pwr") powerDescription=device->item(row,2)->text();
                    if(tag=="err") errorDescription=device->item(row,2)->text();
                    if(tag=="future_tag") {
                        unknownValue=device->item(row,1)->text();
                        unknownDescription=device->item(row,2)->text();
                    }
                }
                for(auto* button:dialog->findChildren<QPushButton*>())
                    if(button->text()=="Bericht kopieren") { button->click(); copied=QApplication::clipboard()->text(); break; }
                const auto png=qEnvironmentVariable("AIRCTRL_TEST_DIAGNOSTICS_PNG");
                if(!png.isEmpty()) screenshotSaved=dialog->grab().save(png);
                inspected=true;
            }
            dialog->accept();
        });
        widget.showDetails();
        QVERIFY(inspected); QCOMPARE(deviceRows,6); QVERIFY(connectionRows>=9);
        QVERIFY(powerDescription.contains("eingeschaltet"));
        QVERIFY(errorDescription.contains("kein gesicherter Wartungsalarm"));
        QCOMPARE(unknownValue,QString("{\"x\":1}"));
        QVERIFY(unknownDescription.contains("Nicht dokumentiertes"));
        QVERIFY(rawText.contains("\"future_tag\"")); QVERIFY(rawText.contains("49236"));
        QVERIFY(copied.contains("ERKLÄRTE GERÄTEWERTE")); QVERIFY(copied.contains("UNVERÄNDERTE ROHDATEN"));
        if(qEnvironmentVariableIsSet("AIRCTRL_TEST_DIAGNOSTICS_PNG")) QVERIFY(screenshotSaved);
        QVERIFY(!QFile::exists(log_));
    }
    void panelCommandsAndReadback() {
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived);
        c.start(); QTRY_COMPARE(status.count(),1);
        const QList<QJsonObject> commands{
            {{"cl",true}}, {{"mode","S"},{"om","s"}}, {{"mode","M"},{"om","3"}},
            {{"func","P"}}, {{"aqil",50}}, {{"uil","0"}}, {{"dt",12}}
        };
        int count=1;
        for(const auto& command : commands) {
            c.setPanelValues(command); ++count; QTRY_COMPARE(status.count(),count);
            const auto received=status.last()[0].toJsonObject();
            for(auto i=command.begin();i!=command.end();++i) QCOMPARE(received[i.key()],i.value());
        }
        const auto requests=calls(); QCOMPARE(requests.size(),15);
        QVERIFY(requests[1].contains("cl=true")); QVERIFY(!requests[1].contains("-I"));
        QVERIFY(requests[3].contains("mode=S")); QVERIFY(requests[3].contains("om=s"));
        QVERIFY(requests[5].contains("mode=M")); QVERIFY(requests[5].contains("om=3"));
        QVERIFY(!requests[5].contains("-I"));
        QVERIFY(requests[9].contains("aqil=50")); QVERIFY(requests[9].contains("-I"));
        QVERIFY(!requests[11].contains("-I")); QVERIFY(requests[13].contains("dt=12"));
        QVERIFY(requests[13].contains("-I")); c.stop();
    }
    void rejectInvalidPanelCommands() {
        Controller c(FAKE_BACKEND); QSignalSpy errors(&c,&Controller::failed);
        for(const auto& value : QList<QJsonObject>{ {{"dt",13}}, {{"aqil",51}}, {{"cl","false"}}, {{"mode","B"}}, {{"aqil",50},{"uil","1"}} })
            c.setPanelValues(value);
        QCOMPARE(errors.count(),5); QVERIFY(!QFile::exists(log_));
    }
    void humidityMenuAndChildLock() {
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* target=widget.findChild<QPushButton*>("humidityTarget");
        QTRY_VERIFY(target->isEnabled());
        bool selected=false;
        QTimer::singleShot(100,[&] {
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if(menu) {
                for(auto* action:menu->actions()) if(action->text()=="60 %") { selected=true; action->trigger(); break; }
                menu->close();
            }
        });
        target->click(); QVERIFY(selected);
        QTRY_VERIFY(target->toolTip().contains("60 %"));
        QCOMPARE(calls().size(),3); QVERIFY(calls()[1].contains("rhset=60")); QVERIFY(calls()[1].contains("-I"));
        auto* lock=widget.findChild<QPushButton*>("childLock"); lock->click();
        QTRY_VERIFY(lock->toolTip().contains("ausschalten"));
        QVERIFY(lock->isEnabled()); QVERIFY(!target->isEnabled());
        lock->click(); QTRY_VERIFY(target->isEnabled());
    }
    void desktopLayerOnX11() {
        if (QGuiApplication::platformName() != "xcb" || qEnvironmentVariable("XDG_SESSION_TYPE")=="wayland")
            QSKIP("Native X11 check requires an X11 session with xcb");
        Preferences p; p.desktop = true;
        Desklet widget(p, FAKE_BACKEND);
        QVERIFY(!widget.testAttribute(Qt::WA_X11NetWmWindowTypeDesktop));
        QVERIFY(widget.testAttribute(Qt::WA_X11NetWmWindowTypeDock));
        QVERIFY(widget.windowFlags().testFlag(Qt::WindowStaysOnBottomHint));
        QCOMPARE(widget.windowType(), Qt::Window);
        widget.show();
        QCoreApplication::processEvents();
        QVERIFY(widget.isVisible());
    }
    void compactLayoutAndLargeFonts() {
        for(int points : {10,24,48}) {
            Preferences p; p.desktop=false; p.valueFont.setPointSize(points);
            Desklet widget(p,FAKE_BACKEND,true);
            widget.applyStatus({{"pwr","1"},{"rh",100},{"rhset",70},{"temp",24.5},{"pm25",999}});
            widget.showAndPosition(); QCoreApplication::processEvents();
            QCOMPARE(widget.findChildren<QPushButton*>().size(),8);
            QVERIFY(widget.findChild<QPushButton*>("menuButton")==nullptr);
            QVERIFY(widget.findChild<QWidget*>("deviceDisplay")==nullptr);
            for(auto* value:widget.findChildren<QLabel*>()) {
                if(!value->isVisible()) continue;
                QCOMPARE(value->font().pointSize(),points);
                QVERIFY(value->fontMetrics().horizontalAdvance(value->text())<=value->width());
                QVERIFY(widget.rect().contains(QRect(value->mapTo(&widget,QPoint()),value->size())));
            }
            if(points==10) { QVERIFY(widget.width()<=340); QVERIFY(widget.height()<110); }
        }
    }
    void backgroundTransparencyAndForeground() {
        for(int transparency : {0,50,100}) {
            Preferences p; p.desktop=false; p.background=QColor("#102030");
            p.foreground=QColor("#abcdef"); p.transparency=transparency;
            Desklet widget(p,FAKE_BACKEND,true);
            widget.applyStatus({{"rh",55},{"rhset",50},{"temp",24},{"pm25",1}});
            widget.showAndPosition(); QCoreApplication::processEvents();
            const auto image=widget.grab().toImage();
            const auto pixel=image.pixelColor(image.width()/2,image.height()-3);
            QVERIFY(qAbs(pixel.alpha()-qRound((100-transparency)*2.55))<=1);
            auto* value=widget.findChild<QLabel*>("value_rh");
            QCOMPARE(value->palette().color(QPalette::WindowText),p.foreground);
            int opaquePixels=0;
            const auto textImage=value->grab().toImage();
            for(int y=0;y<textImage.height();++y) for(int x=0;x<textImage.width();++x)
                if(textImage.pixelColor(x,y).alpha()>240) ++opaquePixels;
            QVERIFY(opaquePixels>5); // background transparency must not fade the text
            QVERIFY(!QFile::exists(log_));
        }
    }
    void contextAppearancePersistsWithoutDeviceWrites() {
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition();
        bool opened=false, changed=false;
        QTimer::singleShot(80,[&] {
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if(!menu) return;
            opened=true;
            auto* action=menu->findChild<QAction*>("appearanceTransparency");
            if(action) {
                QTimer::singleShot(80,[&] {
                    auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                    if(dialog) { changed=true; dialog->setIntValue(75); dialog->accept(); }
                });
                action->trigger();
            }
            menu->close();
        });
        auto* value=widget.findChild<QLabel*>("value_rh");
        QContextMenuEvent event(QContextMenuEvent::Mouse,QPoint(3,3),value->mapToGlobal(QPoint(3,3)));
        QApplication::sendEvent(value,&event);
        QTRY_VERIFY(changed); QVERIFY(opened);
        QCOMPARE(Preferences::load().transparency,75);
        QVERIFY(!QFile::exists(log_));
    }
    void rightClicksReachMenuOnEverySurface() {
        Preferences p; p.desktop=false;
        Desklet widget(p,FAKE_BACKEND); widget.showAndPosition();
        widget.applyStatus({{"pwr","1"},{"rh",55},{"rhset",50}});
        const QList<QWidget*> surfaces{&widget,widget.findChild<QWidget*>("controlBar"),
            widget.findChild<QWidget*>("values"),widget.findChild<QLabel*>("value_rh"),
            widget.findChild<QPushButton*>("power")};
        for(auto* target:surfaces) {
            QVERIFY(target);
            QCOMPARE(clickMenus(target,Qt::RightButton,true),1);
        }
        widget.setConnectionError("offline");
        QCOMPARE(clickMenus(widget.findChild<QPushButton*>("power"),Qt::RightButton),1);
        QVERIFY(!QFile::exists(log_));
    }
    void leftClickWorksEvenWhenPositionLocked() {
        Preferences p; p.locked=true;
        Desklet widget(p,FAKE_BACKEND); widget.showAndPosition();
        const auto position=widget.pos();
        QCOMPARE(clickMenus(widget.findChild<QLabel*>("value_rh"),Qt::LeftButton),1);
        QCOMPARE(widget.pos(),position); QVERIFY(!QFile::exists(log_));
    }
    void draggingMovesAndSavesWithoutOpeningMenu() {
        ScopedEnvironment session("XDG_SESSION_TYPE","x11");
        Preferences p; p.desktop=true; p.position={80,80};
        Desklet widget(p,FAKE_BACKEND); widget.showAndPosition();
        auto* value=widget.findChild<QLabel*>("value_rh");
        const QPoint local(6,6), delta(35,24), oldPosition=widget.pos();
        const auto global=value->mapToGlobal(local);
        int menus=0;
        QTimer closer;
        connect(&closer,&QTimer::timeout,this,[&] {
            if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())) { ++menus; menu->close(); }
        });
        closer.start(25);
        QTest::mousePress(value,Qt::LeftButton,Qt::NoModifier,local);
        QMouseEvent movement(QEvent::MouseMove,QPointF(local+delta),QPointF(global+delta),
                             Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(value,&movement);
        QTest::mouseRelease(value,Qt::LeftButton,Qt::NoModifier,local);
        QTest::qWait(90);
        QCOMPARE(widget.pos(),oldPosition+delta); QCOMPARE(Preferences::load().position,widget.pos());
        QCOMPARE(menus,0); QVERIFY(!QFile::exists(log_));
    }
    void positionDialogSavesCoordinates() {
        ScopedEnvironment session("XDG_SESSION_TYPE","x11");
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition();
        bool changed=false;
        QTimer::singleShot(80,[&] {
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if(!menu) return;
            for(auto* action:menu->actions()) if(action->text()=="Position festlegen …") {
                QTimer::singleShot(80,[&] {
                    auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    if(!dialog) return;
                    auto* x=dialog->findChild<QSpinBox*>("positionX");
                    auto* y=dialog->findChild<QSpinBox*>("positionY");
                    if(x && y) { x->setValue(70); y->setValue(95); changed=true; dialog->accept(); }
                    else dialog->reject();
                });
                action->trigger(); break;
            }
            menu->close();
        });
        QTest::mouseClick(widget.findChild<QLabel*>("value_rh"),Qt::LeftButton);
        QTRY_VERIFY(changed);
        QCOMPARE(widget.pos(),QPoint(70,95)); QCOMPARE(Preferences::load().position,QPoint(70,95));
        QVERIFY(!QFile::exists(log_));
    }
    void waylandDragUsesCompositorAndMenuStillWorks() {
        // Exercise routing under offscreen; this does not emulate a compositor.
        ScopedEnvironment session("XDG_SESSION_TYPE","wayland");
        Preferences p; p.desktop=true; p.position={123,234}; p.save();
        NativeMoveProbe widget(p,FAKE_BACKEND); widget.showAndPosition();
        QCOMPARE(widget.windowType(),Qt::Window);
        QVERIFY(widget.windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(!widget.windowFlags().testFlag(Qt::WindowStaysOnBottomHint));
        QVERIFY(!widget.testAttribute(Qt::WA_X11NetWmWindowTypeDock));
        auto* value=widget.findChild<QLabel*>("value_rh");
        const QPoint local(6,6), delta(35,24), oldPosition=widget.pos();
        const auto global=value->mapToGlobal(local);
        int menus=0; bool offeredPosition=false;
        QTimer closer;
        connect(&closer,&QTimer::timeout,this,[&] {
            if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                ++menus;
                for(auto* action:menu->actions()) {
                    offeredPosition |= action->text()=="Position festlegen …";
                    // Saving a preference must retain prior non-Wayland coordinates.
                    if(action->text()=="Position sperren") action->trigger();
                }
                menu->close();
            }
        });
        closer.start(25);
        QTest::mousePress(value,Qt::LeftButton,Qt::NoModifier,local);
        QMouseEvent movement(QEvent::MouseMove,QPointF(local+delta),QPointF(global+delta),
                             Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(value,&movement);
        QApplication::sendEvent(value,&movement);
        QTest::mouseRelease(value,Qt::LeftButton,Qt::NoModifier,local);
        QTest::qWait(90);
        QCOMPARE(widget.moveRequests,1); QCOMPARE(widget.pos(),oldPosition); QCOMPARE(menus,0);
        QTest::mouseClick(value,Qt::LeftButton); QTest::qWait(90);
        QCOMPARE(menus,1); QVERIFY(!offeredPosition); QVERIFY(Preferences::load().locked);
        QCOMPARE(Preferences::load().position,p.position);
        QTest::mousePress(value,Qt::LeftButton,Qt::NoModifier,local);
        QApplication::sendEvent(value,&movement);
        QTest::mouseRelease(value,Qt::LeftButton,Qt::NoModifier,local);
        QTest::qWait(90);
        QCOMPARE(widget.moveRequests,1); QCOMPARE(menus,1);
        QVERIFY(!QFile::exists(log_));
    }
    void previewNeverWrites() {
        Desklet widget(Preferences{}, FAKE_BACKEND, true);
        widget.applyStatus({{"pwr","1"},{"rhset",50}}); widget.start();
        QVERIFY(!widget.findChild<QPushButton*>("power")->isEnabled());
        QVERIFY(!widget.findChild<QPushButton*>("humidityTarget")->isEnabled());
        QVERIFY(!QFile::exists(log_));
    }
    void settingsRoundtripAndAutostart() {
        Preferences p; p.host = "host.example"; p.port = 5678; p.interval = 15;
        p.desktop = false; p.locked = true; p.position = {42,60};
        p.background=QColor("#334455"); p.foreground=QColor("#ddccbb"); p.transparency=65;
        p.valueFont=QFont("DejaVu Serif",22,QFont::Bold,true); p.visibleValues={"rh","temp","iaql"}; p.save();
        auto q = Preferences::load(); QCOMPARE(q.host,p.host); QCOMPARE(q.port,p.port);
        QCOMPARE(q.interval,p.interval); QCOMPARE(q.position,p.position); QVERIFY(q.locked); QVERIFY(!q.desktop);
        QCOMPARE(q.background,p.background); QCOMPARE(q.foreground,p.foreground); QCOMPARE(q.transparency,p.transparency);
        QCOMPARE(q.valueFont,p.valueFont); QCOMPARE(q.visibleValues,p.visibleValues);
        QString error; QVERIFY2(setAutostart(true, &error), qPrintable(error));
        QFile file(autostartPath()); QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("Exec=\"")); file.close();
        QVERIFY(setAutostart(false)); QVERIFY(!QFile::exists(autostartPath()));
    }
};
QTEST_MAIN(Tests)
#include "test_desklet.moc"
