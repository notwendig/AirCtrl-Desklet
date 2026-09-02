#include "desklet.hpp"
#include "udp_device.hpp"
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
#include <QPainter>

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
        QFile file(log_); if(!file.open(QIODevice::ReadOnly)) return {};
        QList<QJsonArray> out;
        for (const auto& line : file.readAll().split('\n'))
            if (!line.isEmpty()) out.append(QJsonDocument::fromJson(line).array());
        return out;
    }
    static QStringList emblemIds(const QJsonObject& status, bool connected=true) {
        QStringList result;
        for(const auto& emblem:currentEmblems(status,connected)) result.append(emblem.id);
        return result;
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
        qunsetenv("AIRCTRL_TEST_WRITE_GATE"); QFile::remove(temp_.filePath("write.ready"));
        qunsetenv("AIRCTRL_TEST_NOTIFY_GATE"); qunsetenv("AIRCTRL_TEST_TICK_MS");
        qunsetenv("AIRCTRL_TEST_EXIT_FILE");
        QSettings().clear();
    }
    void observationStreamsWithoutPolling() {
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        c.configure("127.0.0.1",12345,5); c.start(); c.start();
        QTRY_VERIFY(status.count()>=3);
        QVERIFY(!c.busy()); QVERIFY(c.observing()); QCOMPARE(c.observationStarts(),quint64(1));
        QCOMPARE(calls().size(),1); QCOMPARE(errors.count(),0);
        const auto args=calls().first();
        QVERIFY(args.contains("status-observe")); QVERIFY(!args.contains("status")); QVERIFY(!args.contains("set"));
        QCOMPARE(args[args.toVariantList().indexOf("--timeout")+1].toString(),QString("60"));
        QCOMPARE(args[args.toVariantList().indexOf("--idle-timeout")+1].toString(),QString("90"));
        c.stop();
    }
    void realisticNineteenSecondPauseStaysOnline() {
        qputenv("AIRCTRL_TEST_TICK_MS","19000");
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* c=widget.findChild<Controller*>();
        auto* power=static_cast<PanelButton*>(widget.findChild<QPushButton*>("power"));
        QSignalSpy status(c,&Controller::statusReceived), errors(c,&Controller::failed);
        QTRY_VERIFY(status.count()>=1);
        QTest::qWait(11000); // deliberately exceed the old 10 s limit
        QCOMPARE(status.count(),1); QCOMPARE(errors.count(),0); QCOMPARE(calls().size(),1);
        QCOMPARE(power->statusColor(),QColor("#2ecc71")); QVERIFY(power->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(status.count()>=2,10000);
        QCOMPARE(calls().size(),1); QCOMPARE(errors.count(),0); c->stop();
    }
    void typedWritesAndReadback() {
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived), accepted(&c,&Controller::controlAccepted);
        c.start(); QTRY_VERIFY(!status.isEmpty());
        c.setPower(false); QTRY_COMPARE(accepted.count(),1); QTRY_VERIFY(!c.busy());
        QCOMPARE(status.last()[0].toJsonObject()["pwr"].toString(),QString("0"));
        c.setHumidity(60); QTRY_COMPARE(accepted.count(),2); QTRY_VERIFY(!c.busy());
        QCOMPARE(status.last()[0].toJsonObject()["rhset"].toInt(),60);
        const auto requests=calls(); QCOMPARE(requests.size(),3);
        QVERIFY(requests[1].contains("pwr=0")); QVERIFY(!requests[1].contains("-I"));
        QVERIFY(requests[2].contains("rhset=60")); QVERIFY(requests[2].contains("-I"));
        QCOMPARE(c.observationStarts(),quint64(1)); c.stop();
    }
    void streamKeepsButtonsEnabledAndWriteRunsOnce() {
        qputenv("AIRCTRL_TEST_WRITE_GATE",temp_.filePath("write.ready").toUtf8());
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* c=widget.findChild<Controller*>();
        QSignalSpy status(c,&Controller::statusReceived), accepted(c,&Controller::controlAccepted);
        QTRY_VERIFY(status.count()>=3);
        const auto buttons=widget.findChildren<QPushButton*>(); QCOMPARE(buttons.size(),8);
        EnablementRecorder allRecorder, powerRecorder;
        for(auto* button:buttons) { QVERIFY(button->isEnabled()); button->installEventFilter(&allRecorder); }
        auto* power=widget.findChild<QPushButton*>("power"); power->installEventFilter(&powerRecorder);
        QTest::qWait(250); QCOMPARE(allRecorder.disabled,0);
        const auto previous=status.count();
        power->click(); power->click();
        for(auto* button:buttons) QCOMPARE(button->isEnabled(),button==power);
        QTRY_COMPARE(calls().size(),2); QTest::qWait(250);
        QCOMPARE(status.count(),previous); QCOMPARE(accepted.count(),0); QVERIFY(c->busy());
        QVERIFY(c->statusCount()>quint64(previous)); // old notifications arrived but cannot confirm
        QFile gate(temp_.filePath("write.ready")); QVERIFY(gate.open(QIODevice::WriteOnly)); gate.close();
        QTRY_COMPARE(accepted.count(),1); QTRY_VERIFY(!c->busy());
        QCOMPARE(power->toolTip(),QString("Gerät aus · Einschalten"));
        QCOMPARE(powerRecorder.disabled,0); QCOMPARE(calls().size(),2);
        QCOMPARE(c->observationStarts(),quint64(1)); c->stop();
    }
    void offlinePowerDoesNotWaitForFirstStatus() {
        qputenv("AIRCTRL_TEST_READ_GATE",temp_.filePath("read.ready").toUtf8());
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* c=widget.findChild<Controller*>();
        auto* power=static_cast<PanelButton*>(widget.findChild<QPushButton*>("power"));
        QSignalSpy status(c,&Controller::statusReceived), accepted(c,&Controller::controlAccepted), errors(c,&Controller::failed);
        QTRY_COMPARE(calls().size(),1); QVERIFY(!c->busy());
        power->click(); power->click(); QTRY_COMPARE(accepted.count(),1);
        QCOMPARE(status.count(),0); QCOMPARE(calls().size(),2);
        QVERIFY(calls()[1].contains("pwr=1")); QCOMPARE(power->statusColor(),QColor("#ff9800"));
        QVERIFY(releaseRead()); QTRY_VERIFY(!status.isEmpty()); QTRY_VERIFY(!c->busy());
        QCOMPARE(power->statusColor(),QColor("#2ecc71")); QCOMPARE(errors.count(),0);
        QCOMPARE(c->observationStarts(),quint64(1)); c->stop();
    }
    void stopCancelsWriteAndObservation() {
        qputenv("AIRCTRL_TEST_READ_GATE",temp_.filePath("read.ready").toUtf8());
        qputenv("AIRCTRL_TEST_WRITE_GATE",temp_.filePath("write.ready").toUtf8());
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived), accepted(&c,&Controller::controlAccepted);
        c.start(); QTRY_COMPARE(calls().size(),1); c.setPower(false); QTRY_COMPARE(calls().size(),2);
        c.stop(); QTest::qWait(150); QCOMPARE(status.count(),0); QCOMPARE(accepted.count(),0); QVERIFY(!c.busy());
        qunsetenv("AIRCTRL_TEST_READ_GATE"); qunsetenv("AIRCTRL_TEST_WRITE_GATE");
        c.start(); QTRY_VERIFY(!status.isEmpty());
        QCOMPARE(calls().size(),3); QVERIFY(calls()[2].contains("status-observe"));
        QCOMPARE(status.last()[0].toJsonObject()["pwr"].toString(),QString("1")); c.stop();
    }
    void stopThenStartDuringStartup() {
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        c.start(); c.stop(); c.start();
        QTRY_VERIFY(!status.isEmpty()); QCOMPARE(errors.count(),0); QVERIFY(c.observing());
        QCOMPARE(c.observationStarts(),quint64(2)); c.stop();
    }
    void manualRefreshRestartsOnlyOneObserver() {
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        c.start(); QTRY_VERIFY(!status.isEmpty());
        c.refresh(); c.refresh(); QTRY_COMPARE(c.observationStarts(),quint64(2));
        QTRY_VERIFY(c.observing()); QTest::qWait(200);
        QCOMPARE(calls().size(),2); QCOMPARE(errors.count(),0); c.stop();
    }
    void observerExitReconnects() {
        qputenv("AIRCTRL_TEST_MODE","exit-stream");
        Controller c(FAKE_BACKEND); c.setReconnectDelay(200);
        QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        c.start(); QTRY_COMPARE(errors.count(),1);
        qputenv("AIRCTRL_TEST_MODE",""); QTRY_COMPARE(c.observationStarts(),quint64(2));
        QTRY_VERIFY(c.observing()); QTest::qWait(200);
        QCOMPARE(errors.count(),1); QCOMPARE(calls().size(),2); c.stop();
    }
    void idleWatchdogReconnects() {
        qputenv("AIRCTRL_TEST_MODE","idle");
        Controller c(FAKE_BACKEND); c.setObservationWatchdogs(1000,200); c.setReconnectDelay(200);
        QSignalSpy errors(&c,&Controller::failed);
        c.start(); QTRY_VERIFY(c.observing()); QTRY_COMPARE(errors.count(),1);
        QVERIFY(!c.observing()); qputenv("AIRCTRL_TEST_MODE","");
        QTRY_COMPARE(c.observationStarts(),quint64(2)); QTRY_VERIFY(c.observing());
        QCOMPARE(calls().size(),2); c.stop();
    }
    void writeFailureDoesNotDropHealthyConnection() {
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* c=widget.findChild<Controller*>();
        auto* power=static_cast<PanelButton*>(widget.findChild<QPushButton*>("power"));
        QSignalSpy errors(c,&Controller::failed), commandErrors(c,&Controller::commandFailed);
        QTRY_VERIFY(c->observing()); qputenv("AIRCTRL_TEST_MODE","write-failure");
        power->click(); QTRY_COMPARE(commandErrors.count(),1);
        QVERIFY(!c->busy()); QVERIFY(power->isEnabled()); QCOMPARE(power->statusColor(),QColor("#2ecc71"));
        QTest::qWait(350); QCOMPARE(calls().size(),2); QCOMPARE(errors.count(),0);
        power->click(); QTRY_COMPARE(commandErrors.count(),2); QCOMPARE(calls().size(),3); c->stop();
    }
    void writeWatchdogNoRetry() {
        Controller c(FAKE_BACKEND); c.setWatchdogInterval(200);
        QSignalSpy errors(&c,&Controller::failed), commandErrors(&c,&Controller::commandFailed);
        c.start(); QTRY_VERIFY(c.observing()); qputenv("AIRCTRL_TEST_MODE","write-timeout");
        c.setPower(false); QTRY_COMPARE(commandErrors.count(),1);
        QVERIFY(!c.busy()); QVERIFY(c.observing()); QTest::qWait(300);
        QCOMPARE(calls().size(),2); QCOMPARE(errors.count(),0); c.stop();
    }
    void confirmationTimeoutUnlocksWithoutRetry() {
        qputenv("AIRCTRL_TEST_MODE","idle");
        Controller c(FAKE_BACKEND); c.setConfirmationTimeout(200);
        QSignalSpy errors(&c,&Controller::failed), commandErrors(&c,&Controller::commandFailed), accepted(&c,&Controller::controlAccepted);
        c.start(); QTRY_VERIFY(c.observing()); c.setPower(false); QTRY_COMPARE(accepted.count(),1);
        QTRY_COMPARE(commandErrors.count(),1); QVERIFY(!c.busy()); QVERIFY(c.observing());
        QTest::qWait(250); QCOMPARE(calls().size(),2); QCOMPARE(errors.count(),0); c.stop();
    }
    void pendingWriteSurvivesObserverReconnectWithoutReplay() {
        qputenv("AIRCTRL_TEST_MODE","exit-stream");
        qputenv("AIRCTRL_TEST_WRITE_GATE",temp_.filePath("write.ready").toUtf8());
        Controller c(FAKE_BACKEND); c.setReconnectDelay(200);
        QSignalSpy errors(&c,&Controller::failed), accepted(&c,&Controller::controlAccepted), status(&c,&Controller::statusReceived);
        c.start(); QTRY_VERIFY(c.observing()); c.setPower(false);
        QTRY_COMPARE(errors.count(),1); QVERIFY(c.busy());
        qputenv("AIRCTRL_TEST_MODE",""); QTRY_COMPARE(c.observationStarts(),quint64(2));
        QTRY_VERIFY(c.observing()); QVERIFY(c.busy());
        QFile gate(temp_.filePath("write.ready")); QVERIFY(gate.open(QIODevice::WriteOnly)); gate.close();
        QTRY_COMPARE(accepted.count(),1); QTRY_VERIFY(!c.busy());
        QCOMPARE(status.last()[0].toJsonObject()["pwr"].toString(),QString("0"));
        int writes=0; for(const auto& request:calls()) if(request.contains("set")) ++writes;
        QCOMPARE(writes,1); c.stop();
    }
    void realUdpObservationAndControlUseSeparateSockets() {
        UdpDevice device;
        Controller c(REAL_BACKEND); c.configure("127.0.0.1",device.port,5);
        QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        QSignalSpy commandErrors(&c,&Controller::commandFailed), accepted(&c,&Controller::controlAccepted);
        c.start(); QTRY_VERIFY(status.count()>=2);
        QCOMPARE(device.subscriptions.load(),1); QCOMPARE(device.syncs.load(),1); QCOMPARE(device.controls.load(),0);
        c.setPower(false); QTRY_COMPARE(accepted.count(),1); QTRY_VERIFY(!c.busy());
        QCOMPARE(status.last()[0].toJsonObject()["pwr"].toString(),QString("0"));
        QCOMPARE(device.subscriptions.load(),1); QCOMPARE(device.syncs.load(),2); QCOMPARE(device.controls.load(),1);
        QCOMPARE(device.cancellations.load(),0); QCOMPARE(errors.count(),0); QCOMPARE(commandErrors.count(),0);
        c.stop(); QTRY_COMPARE(device.cancellations.load(),1); QVERIFY(!device.failed.load());
    }
    void backendExitsWhenWidgetParentIsKilled() {
#ifdef Q_OS_LINUX
        const auto exitPath=temp_.filePath("observer.exited"); qputenv("AIRCTRL_TEST_EXIT_FILE",exitPath.toUtf8());
        QProcess parent; parent.start(PARENT_PROBE,{FAKE_BACKEND});
        QTRY_VERIFY(parent.bytesAvailable()>0); QVERIFY(parent.readAllStandardOutput().contains("ready"));
        // Check an actual clean child exit, independent of /proc PID namespaces.
        QVERIFY(!QFile::exists(exitPath)); parent.kill(); QVERIFY(parent.waitForFinished(1000));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(exitPath),2000);
        QFile file(exitPath); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll().trimmed(),QByteArray("15"));
#else
        QSKIP("Linux parent-death protection");
#endif
    }
    void powerAlwaysEnabledAndStateColours() {
        Preferences p; p.foreground=Qt::white; p.background=Qt::white;
        Desklet widget(p,FAKE_BACKEND); widget.showAndPosition();
        auto* power=static_cast<PanelButton*>(widget.findChild<QPushButton*>("power"));
        EnablementRecorder recorder; power->installEventFilter(&recorder);
        QVERIFY(power->isEnabled()); QCOMPARE(power->statusColor(),QColor("#ff9800"));
        const QJsonObject base{{"rh",55},{"rhset",50},{"temp",24},{"pm25",1},{"cl",true}};
        widget.applyStatus(base); // connected, but no valid pwr: never claim OFF
        QCOMPARE(power->statusColor(),QColor("#ff9800"));
        QVERIFY(power->toolTip().contains("unbekannt"));
        auto state=base; state["pwr"]="0"; widget.applyStatus(state);
        QVERIFY(power->isEnabled()); QCOMPARE(power->statusColor(),QColor("#ffffff"));
        QVERIFY(power->accessibleDescription().contains("Gerät aus"));
        state["pwr"]="1"; widget.applyStatus(state);
        QVERIFY(power->isEnabled()); QCOMPARE(power->statusColor(),QColor("#2ecc71"));
        QVERIFY(power->accessibleDescription().contains("Kindersicherung aktiv"));
        widget.setConnectionError("offline");
        QVERIFY(power->isEnabled()); QCOMPARE(power->statusColor(),QColor("#ff9800"));
        QCOMPARE(recorder.disabled,0); QVERIFY(!QFile::exists(log_));

        // Verify actual filled pixels, not just stored colours, also on a fully
        // transparent panel. Optional contact sheet contains real Qt renders.
        QImage preview(327,450,QImage::Format_ARGB32_Premultiplied); preview.fill(QColor("#dddddd"));
        QPainter painter(&preview); painter.setPen(Qt::black);
        QFont caption=painter.font(); caption.setPixelSize(13); painter.setFont(caption);
        const QStringList names{"Orange · keine Verbindung","Weiß · Gerät aus","Grün · Gerät an"};
        const QList<QColor> colours{QColor("#ff9800"),QColor("#ffffff"),QColor("#2ecc71")};
        for(int transparency : {0,100}) {
            Preferences appearance; appearance.transparency=transparency;
            Desklet rendered(appearance,FAKE_BACKEND,true); rendered.showAndPosition();
            auto* button=static_cast<PanelButton*>(rendered.findChild<QPushButton*>("power"));
            for(int i=0;i<3;++i) {
                auto snapshot=base; snapshot["pwr"]=i==1 ? "0" : "1";
                rendered.applyStatus(snapshot);
                if(i==0) rendered.setConnectionError("offline");
                const auto image=button->grab().toImage(); int matching=0;
                for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
                    if(image.pixelColor(x,y)==colours[i]) ++matching;
                QVERIFY2(matching>30,"Power status disc must be opaque and visible");
                if(transparency==0) {
                    painter.drawText(20,i*150+20,names[i]);
                    painter.drawPixmap(20,i*150+28,rendered.grab());
                }
            }
        }
        painter.end();
        const auto png=qEnvironmentVariable("AIRCTRL_TEST_POWER_PNG");
        if(!png.isEmpty()) QVERIFY(preview.save(png));
    }
    void rejectUnsupportedHumidity() {
        Controller c(FAKE_BACKEND); QSignalSpy errors(&c,&Controller::commandFailed);
        c.setHumidity(55); QCOMPARE(errors.count(),1); QVERIFY(!QFile::exists(log_));
    }
    void badStream_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("bad-json")<<QString("bad-json");
        QTest::newRow("oversized")<<QString("oversized");
    }
    void badStream() {
        QFETCH(QString,mode); qputenv("AIRCTRL_TEST_MODE",mode.toUtf8());
        Controller c(FAKE_BACKEND); QSignalSpy errors(&c,&Controller::failed), status(&c,&Controller::statusReceived);
        c.start(); QTRY_COMPARE(errors.count(),1); QCOMPARE(status.count(),0); QVERIFY(!c.observing());
        QCOMPARE(calls().size(),1); c.stop();
    }
    void initialTimeoutAndRecovery() {
        qputenv("AIRCTRL_TEST_MODE","timeout");
        Controller c(FAKE_BACKEND); c.setObservationWatchdogs(200,1000); c.setReconnectDelay(200);
        QSignalSpy errors(&c,&Controller::failed);
        c.start(); QTRY_COMPARE(errors.count(),1); QVERIFY(!c.observing()); QVERIFY(!c.busy());
        qputenv("AIRCTRL_TEST_MODE",""); QTRY_VERIFY(c.observing());
        QCOMPARE(calls().size(),2); c.stop();
    }
    void stopCancelsReconnect() {
        qputenv("AIRCTRL_TEST_MODE","failure");
        Controller c(FAKE_BACKEND); c.setReconnectDelay(200);
        QSignalSpy errors(&c,&Controller::failed);
        c.start(); QTRY_COMPARE(errors.count(),1); c.stop();
        QTest::qWait(400); QCOMPARE(calls().size(),1);
    }
    void streamFragmentsAndBatches_data() {
        QTest::addColumn<QString>("mode"); QTest::addColumn<int>("count");
        QTest::newRow("split-line")<<QString("partial")<<1;
        QTest::newRow("three-lines")<<QString("batch")<<3;
    }
    void streamFragmentsAndBatches() {
        QFETCH(QString,mode); QFETCH(int,count); qputenv("AIRCTRL_TEST_MODE",mode.toUtf8());
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        c.start(); QTRY_COMPARE(status.count(),count); QCOMPARE(errors.count(),0); QVERIFY(c.observing());
        if(mode=="batch") QCOMPARE(status.last()[0].toJsonObject()["rh"].toInt(),43);
        else QCOMPARE(status.last()[0].toJsonObject()["rh"].toInt(),55);
        QCOMPARE(calls().size(),1); c.stop();
    }
    void missingBackend() {
        Controller c(temp_.filePath("missing")); QSignalSpy errors(&c,&Controller::failed);
        c.start(); QCOMPARE(errors.count(),1); QVERIFY(!c.busy()); QVERIFY(!c.observing()); c.stop();
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
        QVERIFY(widget.findChild<QPushButton*>("power")->isEnabled());
        QVERIFY(!target->isEnabled());
        QVERIFY(widget.toolTip().contains("Keine Verbindung"));
        QVERIFY(widget.toolTip().contains("offline"));
        auto* value=widget.findChild<QLabel*>("value_rh");
        QCOMPARE(value->text(),QString("Feuchte 55 %"));
        QVERIFY(value->accessibleDescription().contains("Letzter Empfang"));
        QVERIFY(value->palette().color(QPalette::WindowText).alpha()<255);
        widget.applyStatus({{"name","other"}});
        QCOMPARE(value->text(),QString("Feuchte —"));
        QVERIFY(widget.findChild<QPushButton*>("power")->isEnabled());
        QVERIFY(!target->isEnabled());
    }
    void emblemModeMapping_data() {
        QTest::addColumn<QString>("mode"); QTest::addColumn<QString>("fan");
        QTest::addColumn<int>("icon"); QTest::addColumn<QString>("badge");
        QTest::newRow("auto-not-sleep")<<QString("P")<<QString("s")<<int(EmblemIcon::Auto)<<QString();
        QTest::newRow("sleep")<<QString("S")<<QString("s")<<int(EmblemIcon::Sleep)<<QString();
        QTest::newRow("allergen")<<QString("A")<<QString("2")<<int(EmblemIcon::Allergen)<<QString();
        QTest::newRow("manual-1")<<QString("M")<<QString("1")<<int(EmblemIcon::Fan)<<QString("1");
        QTest::newRow("manual-2")<<QString("M")<<QString("2")<<int(EmblemIcon::Fan)<<QString("2");
        QTest::newRow("manual-3")<<QString("M")<<QString("3")<<int(EmblemIcon::Fan)<<QString("3");
        QTest::newRow("turbo")<<QString("M")<<QString("t")<<int(EmblemIcon::Fan)<<QString("T");
    }
    void emblemModeMapping() {
        QFETCH(QString,mode); QFETCH(QString,fan); QFETCH(int,icon); QFETCH(QString,badge);
        const auto states=currentEmblems({{"pwr","1"},{"mode",mode},{"om",fan}},true);
        QCOMPARE(states.size(),2); QCOMPARE(states.first().id,QString("mode"));
        QCOMPARE(int(states.first().icon),icon); QCOMPARE(states.first().badge,badge);
        QCOMPARE(states.last().id,QString("wifi"));
    }
    void emblemFunctionsDisplayAndPower() {
        QJsonObject status{{"pwr","1"},{"mode","P"},{"func","PH"},{"cl",true},{"dt",8},{"ddp","1"}};
        QCOMPARE(emblemIds(status),QStringList({"lock","mode","function","display","timer","wifi"}));
        auto states=currentEmblems(status,true);
        QCOMPARE(states[2].icon,EmblemIcon::Humidify); QCOMPARE(states[3].icon,EmblemIcon::PM25);
        QVERIFY(states[4].description.contains("keine Restzeit"));
        status["func"]="P"; status["ddp"]="0";
        states=currentEmblems(status,true);
        QCOMPARE(states[2].icon,EmblemIcon::Purify); QCOMPARE(states[3].icon,EmblemIcon::IAI);
        status["ddp"]="3"; status["dt"]=0; status["cl"]=false;
        QCOMPARE(emblemIds(status),QStringList({"mode","function","wifi"}));
        status["mode"]="unknown"; status["func"]="unknown";
        QCOMPARE(emblemIds(status),QStringList({"wifi"}));
        status["pwr"]="0"; status["mode"]="P"; status["func"]="PH"; status["cl"]=true; status["dt"]=8;
        QCOMPARE(emblemIds(status),QStringList({"lock","wifi"}));
        status.remove("pwr"); QCOMPARE(emblemIds(status),QStringList({"lock","wifi"}));
    }
    void emblemAlarmsAreConservative() {
        QJsonObject status{{"pwr","1"},{"modelid","AC2729/10"},{"func","PH"},
            {"wl",100},{"err",49236},{"fltsts0",357},{"fltsts1",119},{"fltsts2",119},{"wicksts",119},
            {"fltt1","A3"},{"fltt2","C7"}};
        QCOMPARE(emblemIds(status),QStringList({"function","wifi"}));
        status["err"]=49408; QVERIFY(emblemIds(status).contains("water"));
        status["err"]=32768; QVERIFY(!emblemIds(status).contains("water")); // tank-open != refill alarm
        status["err"]=49153; QVERIFY(emblemIds(status).contains("clean"));
        status["err"]=49155; QVERIFY(emblemIds(status).contains("clean"));
        status["err"]=49236; status["wl"]=0; status["fltsts0"]=0; status["fltsts1"]=0;
        QCOMPARE(emblemIds(status),QStringList({"function","filter","water","clean","wifi"}));
        for(const auto& state:currentEmblems(status,true)) {
            if(state.id=="filter" || state.id=="water" || state.id=="clean") QVERIFY(state.warning);
        }
        status["func"]="P"; QVERIFY(!emblemIds(status).contains("water"));
        status["fltsts0"]=123; status["fltsts1"]=123;
        status["wicksts"]="0"; status["fltsts2"]="0";
        QVERIFY(emblemIds(status).contains("clean")); QVERIFY(emblemIds(status).contains("filter"));
        status["modelid"]="AC9999/10";
        QCOMPARE(emblemIds(status),QStringList({"function","wifi"}));
        status.remove("modelid"); status["type"]="AC2729";
        QVERIFY(emblemIds(status).contains("filter"));
    }
    void emblemInvalidValuesDoNotCreateAlarms() {
        for(const QJsonValue& value:QList<QJsonValue>{QJsonValue(QJsonValue::Undefined),QJsonValue(),
                false,true,"", "bad", "NaN", -1, 0.5}) {
            QJsonObject status{{"pwr","1"},{"modelid","AC2729/10"},{"func","PH"}};
            for(const auto& key:{"wl","err","fltsts0","fltsts1","fltsts2","wicksts","dt","ddp","cl"}) status[key]=value;
            // cl=true is the only meaningful Boolean in this collection.
            status["cl"]="true";
            QCOMPARE(emblemIds(status),QStringList({"function","wifi"}));
        }
        QCOMPARE(emblemIds({},false),QStringList({"wifi"}));
        QCOMPARE(emblemIds({{"pwr","1"},{"mode","M"},{"om","unexpected"}}),QStringList({"wifi"}));
    }
    void emblemsWaitForConfirmedState() {
        qputenv("AIRCTRL_TEST_WRITE_GATE",temp_.filePath("write.ready").toUtf8());
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* mode=widget.findChild<Emblem*>("emblem_mode");
        auto* c=widget.findChild<Controller*>();
        QTRY_VERIFY(mode->isVisible()); QCOMPARE(mode->icon(),EmblemIcon::Auto);
        widget.findChild<QPushButton*>("power")->click();
        QTest::qWait(200); QVERIFY(mode->isVisible()); QCOMPARE(mode->icon(),EmblemIcon::Auto);
        QFile gate(temp_.filePath("write.ready")); QVERIFY(gate.open(QIODevice::WriteOnly)); gate.close();
        QTRY_VERIFY(!c->busy()); QVERIFY(mode->isHidden());
        QCOMPARE(c->observationStarts(),quint64(1)); QCOMPARE(calls().size(),2); c->stop();
    }
    void emblemOfflineAppearanceAndGeometry() {
        Preferences p; p.desktop=false; p.foreground=QColor("#2468ac"); p.transparency=100;
        Desklet widget(p,FAKE_BACKEND,true); widget.showAndPosition();
        auto* mode=widget.findChild<Emblem*>("emblem_mode"); auto* wifi=widget.findChild<Emblem*>("emblem_wifi");
        auto* bar=widget.findChild<QWidget*>("emblemBar");
        QVERIFY(mode->isHidden()); QVERIFY(wifi->toolTip().contains("noch kein Status"));
        const auto initial=widget.size();
        widget.applyStatus({{"pwr","1"},{"mode","P"},{"func","PH"},{"cl",true}});
        QCoreApplication::processEvents();
        QVERIFY(mode->isVisible()); QCOMPARE(mode->ink(),p.foreground); QVERIFY(!mode->stale());
        QVERIFY(widget.findChild<QWidget*>("controlBar")->geometry().bottom()<bar->geometry().top());
        QVERIFY(bar->geometry().bottom()<widget.findChild<QWidget*>("values")->geometry().top());
        QCOMPARE(initial,widget.size());
        const auto onlineWifi=wifi->grab().toImage();
        // Inspect the composed top-level window; grabbing a plain child in
        // isolation can synthesize an opaque palette background in Qt.
        const auto transparent=widget.grab().toImage();
        QCOMPARE(transparent.pixelColor(bar->mapTo(&widget,QPoint(0,0))).alpha(),0);
        widget.setConnectionError("offline"); QCoreApplication::processEvents();
        QVERIFY(mode->isVisible()); QVERIFY(mode->stale()); QVERIFY(mode->ink().alpha()<255);
        QVERIFY(mode->accessibleDescription().contains("Letzter bestätigter"));
        QVERIFY(!wifi->stale()); QCOMPARE(wifi->ink(),QColor("#d97706"));
        QVERIFY(wifi->grab().toImage()!=onlineWifi); QCOMPARE(initial,widget.size());
        widget.applyStatus({{"pwr","0"}}); QVERIFY(mode->isHidden()); QCOMPARE(initial,widget.size());
        QVERIFY(!QFile::exists(log_));
    }
    void emblemPreviewAndLargeFonts() {
        const QList<QJsonObject> samples{
            {{"mode","P"},{"func","PH"}},
            {{"mode","S"},{"func","P"}},
            {{"mode","A"},{"func","PH"},{"cl",true},{"dt",8}},
            {{"mode","M"},{"om","3"},{"func","P"},{"ddp","1"}},
            {{"mode","P"},{"func","PH"},{"cl",true},{"dt",12},{"ddp","0"},
                {"wl",0},{"fltsts0",0},{"fltsts1",0}},
        };
        QImage preview(347,samples.size()*154,QImage::Format_ARGB32_Premultiplied); preview.fill(QColor("#dddddd"));
        QPainter painter(&preview); painter.setPen(Qt::black);
        QFont caption=painter.font(); caption.setPixelSize(13); painter.setFont(caption);
        const QStringList names{"Automatik · 2-in-1","Ruhemodus · Luftreinigung","Allergen · Kindersicherung · Timer",
            "Manuell · Stufe 3 · PM2.5","Wartungshinweise · Testdaten"};
        for(int points:{10,24,48}) {
            Preferences p; p.valueFont.setPointSize(points);
            Desklet widget(p,FAKE_BACKEND,true); widget.showAndPosition();
            for(int i=0;i<samples.size();++i) {
                auto sample=samples[i]; sample["pwr"]="1"; sample["modelid"]="AC2729/10";
                sample["rh"]=55; sample["rhset"]=50; sample["temp"]=24; sample["pm25"]=1;
                widget.applyStatus(sample); QCoreApplication::processEvents();
                for(auto* emblem:widget.findChildren<Emblem*>()) if(emblem->isVisible()) {
                    QVERIFY(widget.rect().contains(QRect(emblem->mapTo(&widget,QPoint()),emblem->size())));
                    QCOMPARE(emblem->width(),emblem->height());
                }
                if(points==10) {
                    QCOMPARE(widget.width(),287); QCOMPARE(widget.height(),114);
                    painter.drawText(20,i*154+20,names[i]); painter.drawPixmap(20,i*154+28,widget.grab());
                }
            }
        }
        painter.end();
        const auto png=qEnvironmentVariable("AIRCTRL_TEST_EMBLEMS_PNG"); if(!png.isEmpty()) QVERIFY(preview.save(png));
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
        c.start(); QTRY_VERIFY(!status.isEmpty());
        const QList<QJsonObject> commands{
            {{"cl",true}}, {{"mode","S"},{"om","s"}}, {{"mode","M"},{"om","3"}},
            {{"func","P"}}, {{"aqil",50}}, {{"uil","0"}}, {{"dt",12}}
        };
        for(const auto& command:commands) {
            c.setPanelValues(command); QTRY_VERIFY(!c.busy());
            const auto received=status.last()[0].toJsonObject();
            for(auto i=command.begin();i!=command.end();++i) QCOMPARE(received[i.key()],i.value());
        }
        const auto requests=calls(); QCOMPARE(requests.size(),8);
        QVERIFY(requests[1].contains("cl=true")); QVERIFY(!requests[1].contains("-I"));
        QVERIFY(requests[2].contains("mode=S")); QVERIFY(requests[2].contains("om=s"));
        QVERIFY(requests[3].contains("mode=M")); QVERIFY(requests[3].contains("om=3"));
        QVERIFY(requests[5].contains("aqil=50")); QVERIFY(requests[5].contains("-I"));
        QVERIFY(!requests[6].contains("-I")); QVERIFY(requests[7].contains("dt=12")); QVERIFY(requests[7].contains("-I"));
        QCOMPARE(c.observationStarts(),quint64(1)); c.stop();
    }
    void rejectInvalidPanelCommands() {
        Controller c(FAKE_BACKEND); QSignalSpy errors(&c,&Controller::commandFailed);
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
        QCOMPARE(calls().size(),2); QVERIFY(calls()[1].contains("rhset=60")); QVERIFY(calls()[1].contains("-I"));
        auto* lock=widget.findChild<QPushButton*>("childLock"); lock->click();
        QTRY_VERIFY(lock->toolTip().contains("ausschalten"));
        QVERIFY(lock->isEnabled()); QVERIFY(!target->isEnabled());
        auto* power=widget.findChild<QPushButton*>("power"); QVERIFY(power->isEnabled());
        power->click(); QTRY_VERIFY(power->toolTip().contains("Gerät aus"));
        QVERIFY(lock->isEnabled()); QVERIFY(lock->toolTip().contains("ausschalten"));
        QVERIFY(!target->isEnabled());
        power->click(); QTRY_VERIFY(power->toolTip().contains("Gerät an"));
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
            if(points==10) { QVERIFY(widget.width()<=340); QVERIFY(widget.height()<125); }
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
            widget.findChild<QWidget*>("emblemBar"),widget.findChild<Emblem*>("emblem_wifi"),
            widget.findChild<QWidget*>("values"),widget.findChild<QLabel*>("value_rh"),
            widget.findChild<QPushButton*>("power")};
        for(auto* target:surfaces) {
            QVERIFY(target);
            QCOMPARE(clickMenus(target,Qt::RightButton,true),1);
        }
        widget.setConnectionError("offline");
        QCOMPARE(clickMenus(widget.findChild<QPushButton*>("power"),Qt::RightButton),1);
        QCOMPARE(clickMenus(widget.findChild<QPushButton*>("humidityTarget"),Qt::RightButton),1);
        QVERIFY(!QFile::exists(log_));
    }
    void leftClickWorksEvenWhenPositionLocked() {
        Preferences p; p.locked=true;
        Desklet widget(p,FAKE_BACKEND); widget.showAndPosition();
        const auto position=widget.pos();
        QCOMPARE(clickMenus(widget.findChild<QLabel*>("value_rh"),Qt::LeftButton),1);
        QCOMPARE(clickMenus(widget.findChild<Emblem*>("emblem_wifi"),Qt::LeftButton),1);
        QCOMPARE(widget.pos(),position); QVERIFY(!QFile::exists(log_));
    }
    void draggingMovesAndSavesWithoutOpeningMenu_data() {
        QTest::addColumn<QString>("surface");
        QTest::newRow("value")<<QString("value_rh");
        QTest::newRow("emblem")<<QString("emblem_wifi");
    }
    void draggingMovesAndSavesWithoutOpeningMenu() {
        QFETCH(QString,surface);
        ScopedEnvironment session("XDG_SESSION_TYPE","x11");
        Preferences p; p.desktop=true; p.position={80,80};
        Desklet widget(p,FAKE_BACKEND); widget.showAndPosition();
        auto* value=widget.findChild<QWidget*>(surface); QVERIFY(value);
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
        auto* power=widget.findChild<QPushButton*>("power");
        QVERIFY(power->isEnabled()); power->click();
        QVERIFY(power->toolTip().contains("Vorschau"));
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
