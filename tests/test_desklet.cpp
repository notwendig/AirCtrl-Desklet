#include "desklet.hpp"
#include "airctrl_version.hpp"
#include "diagnostics.hpp"
#include "udp_device.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
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
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QClipboard>
#include <QPainter>
#include <QPointer>
#include <QCheckBox>
#include <QDBusConnection>
#include <QDBusContext>

class MonitoringProbe : public Desklet {
public:
    using Desklet::Desklet;
    qint64 now=0;
    int deliveries=0;
    void advance(qint64 seconds) { now+=seconds*1000; updateMonitoring(); }
    void refreshMonitor() { updateMonitoring(); }
protected:
    qint64 monotonicMs() const override { return now; }
    void deliverAlarm(const QString&,bool) override { ++deliveries; }
};

class NotificationProbe : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.freedesktop.Notifications")
public:
    int received=0;
    QString body;
    QVariantMap hints;
public slots:
    uint Notify(const QString&,uint,const QString&,const QString&,const QString& text,
                const QStringList&,const QVariantMap& properties,int) {
        ++received; body=text; hints=properties; return uint(received);
    }
};

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
    ~EnablementRecorder() override {
        for(auto& object:watched_) if(object) object->removeEventFilter(this);
    }
    void watch(QObject* object) {
        watched_.append(object); object->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type()==QEvent::EnabledChange && !static_cast<QWidget*>(watched)->isEnabled()) ++disabled;
        return false;
    }
private:
    QList<QPointer<QObject>> watched_;
};

class Tests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir temp_;
    QString log_, state_;
    int testSequence_=0;
    void setMode(const QString& mode) {
        qputenv("AIRCTRL_TEST_MODE",mode.toUtf8());
        QFile file(temp_.filePath("mode.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));
        file.write(mode.toUtf8());
    }
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
        const auto probePath=temp_.filePath("unix-socket-probe");
        QLocalServer::removeServer(probePath);
        QLocalServer probe;
        if(!probe.listen(probePath)) QSKIP("Die Testumgebung erlaubt keine lokalen Unix-Sockets.");
        probe.close(); QLocalServer::removeServer(probePath);
        QCoreApplication::setOrganizationName("AirControlTests");
        QCoreApplication::setApplicationName("AirControlTests");
        QCoreApplication::setApplicationVersion(AIRCTRL_VERSION);
        QApplication::setStyle("Fusion");
        qputenv("XDG_CONFIG_HOME", temp_.path().toUtf8());
        log_ = temp_.filePath("calls.jsonl"); state_ = temp_.filePath("state.json");
        qputenv("AIRCTRL_TEST_LOG", log_.toUtf8()); qputenv("AIRCTRL_TEST_STATE", state_.toUtf8());
        qputenv("AIRCTRL_TEST_MODE_FILE",temp_.filePath("mode.txt").toUtf8());
        qputenv("AIRCTRL_SERVER_EXIT_ON_IDLE","1");
        qputenv("AIRCTRL_TEST_SUPPRESS_DESKTOP_ALARMS","1");
    }
    void init() {
        setMode(""); QFile::remove(log_); QFile::remove(state_);
        qputenv("AIRCTRL_SOCKET",temp_.filePath(QString("server-%1.sock").arg(++testSequence_)).toUtf8());
        qunsetenv("AIRCTRL_TEST_READ_GATE"); QFile::remove(temp_.filePath("read.ready"));
        qunsetenv("AIRCTRL_TEST_WRITE_GATE"); QFile::remove(temp_.filePath("write.ready"));
        qunsetenv("AIRCTRL_TEST_NOTIFY_GATE"); qunsetenv("AIRCTRL_TEST_TICK_MS");
        qunsetenv("AIRCTRL_TEST_EXIT_FILE");
        QSettings().clear();
        QFile::remove(AutomationEngine::scriptPath());
    }
    void observationStreamsWithoutPolling() {
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        c.configure("127.0.0.1",12345,5); c.start(); c.start();
        QTRY_VERIFY(status.count()>=3);
        QVERIFY(!c.busy()); QVERIFY(c.observing()); QCOMPARE(c.observationStarts(),quint64(1));
        QCOMPARE(calls().size(),1); QCOMPARE(errors.count(),0);
        const auto args=calls().first();
        QVERIFY(args.contains("session")); QVERIFY(!args.contains("status")); QVERIFY(!args.contains("set"));
        QCOMPARE(args[args.toVariantList().indexOf("--timeout")+1].toString(),QString("60"));
        QCOMPARE(args[args.toVariantList().indexOf("--control-timeout")+1].toString(),QString("10"));
        QCOMPARE(args[args.toVariantList().indexOf("--idle-timeout")+1].toString(),QString("90"));
        c.stop();
    }
    void defaultHostIsAc2729Dash10() {
        QCOMPARE(Preferences{}.host, QString("AC2729-10"));
        QCOMPARE(Preferences::load().host, QString("AC2729-10"));
        QSettings settings;
        settings.setValue("device/host", "ac2729/10");
        QCOMPARE(Preferences::load().host, QString("AC2729-10"));
        QCOMPARE(settings.value("device/host").toString(), QString("AC2729-10"));
        settings.setValue("device/host", "AC2729_10");
        QCOMPARE(Preferences::load().host, QString("AC2729-10"));
        settings.setValue("device/host", "192.168.77.5");
        QCOMPARE(Preferences::load().host, QString("192.168.77.5"));
        Controller controller(FAKE_BACKEND);
        QCOMPARE(controller.host(), QString("AC2729-10"));
    }
    void realisticNineteenSecondPauseStaysOnline() {
        qputenv("AIRCTRL_TEST_TICK_MS","19000");
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* c=widget.findChild<Controller*>();
        auto* power=static_cast<PanelButton*>(widget.findChild<QPushButton*>("power"));
        QSignalSpy status(c,&Controller::statusReceived), errors(c,&Controller::failed);
        QTRY_VERIFY(status.count()>=1);
        QTest::qWait(11000); // deliberately exceed the old 10 s limit
        QVERIFY(widget.dataAgeSeconds()>=10);
        QCOMPARE(widget.findChild<MonitorBar*>()->freshness(),DataFreshness::Fresh);
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
    void hostnamesAndIpAddressesReachBackendUnchanged() {
        const QList<QPair<QString,QString>> hosts{
            {" 192.0.2.10 ","192.0.2.10"},
            {" luftreiniger.local ","luftreiniger.local"},
            {" [2001:db8::5] ","[2001:db8::5]"},
        };
        for(const auto& item:hosts) {
            QFile::remove(log_);
            qputenv("AIRCTRL_SOCKET",temp_.filePath(QString("server-%1.sock").arg(++testSequence_)).toUtf8());
            Controller c(FAKE_BACKEND); c.configure(item.first,5683,5);
            QCOMPARE(c.host(),item.second); c.start();
            QTRY_COMPARE_WITH_TIMEOUT(calls().size(),1,15000);
            const auto args=calls().first(); int hostArgument=-1;
            for(int i=0;i<args.size();++i) if(args[i].toString()=="-H") hostArgument=i;
            QVERIFY(hostArgument>=0); QVERIFY(hostArgument+1<args.size());
            QCOMPARE(args[hostArgument+1].toString(),item.second); c.stop();
        }
    }
    void luaStatusEventUsesConfirmedWritePathOnce() {
        QVERIFY(QDir().mkpath(QFileInfo(AutomationEngine::scriptPath()).absolutePath()));
        QFile script(AutomationEngine::scriptPath());
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(R"lua(
            function on_event(event)
                if event.type == "status" and event.changed.rh then
                    airctrl.set { func = "P" }
                end
            end
        )lua");
        script.close();

        Preferences preferences;
        preferences.automationEnabled = true;
        Desklet widget(preferences,FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* controller=widget.findChild<Controller*>();
        auto* automation=widget.findChild<AutomationEngine*>();
        QVERIFY(automation); QVERIFY(automation->loaded());
        QSignalSpy commandErrors(controller,&Controller::commandFailed);

        QTRY_COMPARE(calls().size(),2);
        QTRY_VERIFY(!controller->busy());
        QTRY_VERIFY(automation->lastAction().contains("bestätigt"));
        QTest::qWait(350);

        QCOMPARE(calls().size(),2);
        QCOMPARE(commandErrors.size(),0);
        QCOMPARE(calls()[1].contains("func=P"),true);
        controller->stop();
    }
    void streamKeepsButtonsEnabledAndWriteRunsOnce() {
        qputenv("AIRCTRL_TEST_WRITE_GATE",temp_.filePath("write.ready").toUtf8());
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* c=widget.findChild<Controller*>();
        QSignalSpy status(c,&Controller::statusReceived), accepted(c,&Controller::controlAccepted);
        QTRY_VERIFY(status.count()>=3);
        const auto buttons=widget.findChildren<QPushButton*>(); QCOMPARE(buttons.size(),8);
        EnablementRecorder allRecorder, powerRecorder;
        for(auto* button:buttons) { QVERIFY(button->isEnabled()); allRecorder.watch(button); }
        auto* power=widget.findChild<QPushButton*>("power"); powerRecorder.watch(power);
        QTest::qWait(250); QCOMPARE(allRecorder.disabled,0);
        const auto previous=status.count();
        power->click(); power->click();
        for(auto* button:buttons) QCOMPARE(button->isEnabled(),button==power);
        QTRY_COMPARE(calls().size(),2); QTest::qWait(250);
        QCOMPARE(status.count(),previous); QCOMPARE(accepted.count(),0); QVERIFY(c->busy());
        QCOMPARE(c->statusCount(),quint64(previous)); // one socket pauses Observe while control is pending
        QFile gate(temp_.filePath("write.ready")); QVERIFY(gate.open(QIODevice::WriteOnly)); gate.close();
        QTRY_COMPARE(accepted.count(),1); QTRY_VERIFY(!c->busy());
        QCOMPARE(power->toolTip(),QString("Gerät aus · Einschalten"));
        QCOMPARE(powerRecorder.disabled,0); QCOMPARE(calls().size(),2);
        QCOMPARE(c->observationStarts(),quint64(1)); c->stop();
    }
    void offlinePowerIsRejectedUntilServerHasStatus() {
        qputenv("AIRCTRL_TEST_READ_GATE",temp_.filePath("read.ready").toUtf8());
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* c=widget.findChild<Controller*>();
        auto* power=static_cast<PanelButton*>(widget.findChild<QPushButton*>("power"));
        QSignalSpy status(c,&Controller::statusReceived), accepted(c,&Controller::controlAccepted),
            errors(c,&Controller::failed),commandErrors(c,&Controller::commandFailed);
        QTRY_COMPARE(calls().size(),1); QVERIFY(!c->busy());
        power->click(); power->click(); QTRY_COMPARE(commandErrors.count(),2);
        QCOMPARE(accepted.count(),0); QCOMPARE(status.count(),0); QCOMPARE(calls().size(),1);
        QCOMPARE(power->statusColor(),QColor("#ff9800"));
        QVERIFY(releaseRead()); QTRY_VERIFY(!status.isEmpty()); QTRY_VERIFY(!c->busy());
        QCOMPARE(power->statusColor(),QColor("#2ecc71")); QCOMPARE(errors.count(),0);
        QCOMPARE(c->observationStarts(),quint64(1)); c->stop();
    }
    void stopCancelsWriteAndObservation() {
        qputenv("AIRCTRL_TEST_WRITE_GATE",temp_.filePath("write.ready").toUtf8());
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived), accepted(&c,&Controller::controlAccepted);
        c.start(); QTRY_VERIFY(!status.isEmpty()); QCOMPARE(calls().size(),1);
        c.setPower(false); QTRY_COMPARE(calls().size(),2);
        const auto beforeStop=status.count(); c.stop(); QTest::qWait(150);
        QCOMPARE(status.count(),beforeStop); QCOMPARE(accepted.count(),0); QVERIFY(!c.busy());
        qunsetenv("AIRCTRL_TEST_WRITE_GATE");
        c.start(); QTRY_VERIFY(status.count()>beforeStop);
        QCOMPARE(calls().size(),3); QVERIFY(calls()[2].contains("session"));
        QCOMPARE(status.last()[0].toJsonObject()["pwr"].toString(),QString("1")); c.stop();
    }
    void stopThenStartDuringStartup() {
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        c.start(); c.stop(); c.start();
        QTRY_VERIFY(!status.isEmpty()); QCOMPARE(errors.count(),0); QVERIFY(c.observing());
        QCOMPARE(c.observationStarts(),quint64(1)); c.stop();
    }
    void manualRefreshRestartsOnlyOneObserver() {
        Controller c(FAKE_BACKEND); QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        c.start(); QTRY_VERIFY(!status.isEmpty());
        c.refresh(); c.refresh(); QTRY_COMPARE(c.observationStarts(),quint64(2));
        QTRY_VERIFY(c.observing()); QTest::qWait(200);
        QCOMPARE(calls().size(),2); QCOMPARE(errors.count(),0); c.stop();
    }
    void observerExitReconnects() {
        setMode("exit-stream");
        Controller c(FAKE_BACKEND); c.setReconnectDelay(200);
        QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        c.start(); QTRY_COMPARE(errors.count(),1);
        setMode(""); QTRY_COMPARE(c.observationStarts(),quint64(2));
        QTRY_VERIFY(c.observing()); QTest::qWait(200);
        QCOMPARE(errors.count(),1); QCOMPARE(calls().size(),2); c.stop();
    }
    void idleWatchdogReconnects() {
        setMode("idle");
        Controller c(FAKE_BACKEND); c.setObservationWatchdogs(1000,200); c.setReconnectDelay(200);
        QSignalSpy errors(&c,&Controller::failed);
        c.start(); QTRY_VERIFY(c.observing()); QTRY_COMPARE(errors.count(),1);
        QVERIFY(!c.observing()); setMode("");
        QTRY_COMPARE(c.observationStarts(),quint64(2)); QTRY_VERIFY(c.observing());
        QCOMPARE(calls().size(),2); c.stop();
    }
    void writeFailureDoesNotDropHealthyConnection() {
        setMode("write-failure");
        Desklet widget(Preferences{},FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* c=widget.findChild<Controller*>();
        auto* power=static_cast<PanelButton*>(widget.findChild<QPushButton*>("power"));
        QSignalSpy errors(c,&Controller::failed), commandErrors(c,&Controller::commandFailed);
        QTRY_VERIFY(c->observing());
        power->click(); QTRY_COMPARE(commandErrors.count(),1);
        QVERIFY(!c->busy()); QVERIFY(power->isEnabled()); QCOMPARE(power->statusColor(),QColor("#2ecc71"));
        QTest::qWait(350); QCOMPARE(calls().size(),2); QCOMPARE(errors.count(),0);
        power->click(); QTRY_COMPARE(commandErrors.count(),2); QCOMPARE(calls().size(),3); c->stop();
    }
    void writeWatchdogNoRetry() {
        setMode("write-timeout");
        Controller c(FAKE_BACKEND); c.setWatchdogInterval(200);
        QSignalSpy errors(&c,&Controller::failed), commandErrors(&c,&Controller::commandFailed);
        c.start(); QTRY_VERIFY(c.observing());
        c.setPower(false); QTRY_COMPARE(commandErrors.count(),1);
        QVERIFY(!c.busy()); QVERIFY(c.observing()); QTest::qWait(300);
        QCOMPARE(calls().size(),2); QCOMPARE(errors.count(),0); c.stop();
    }
    void confirmationTimeoutUnlocksWithoutRetry() {
        setMode("idle");
        Controller c(FAKE_BACKEND); c.setConfirmationTimeout(200);
        QSignalSpy errors(&c,&Controller::failed), commandErrors(&c,&Controller::commandFailed), accepted(&c,&Controller::controlAccepted);
        c.start(); QTRY_VERIFY(c.observing()); c.setPower(false); QTRY_COMPARE(accepted.count(),1);
        QTRY_COMPARE(commandErrors.count(),1); QVERIFY(!c.busy()); QVERIFY(c.observing());
        QTest::qWait(250); QCOMPARE(calls().size(),2); QCOMPARE(errors.count(),0); c.stop();
    }
    void pendingWriteFailsWhenIoSessionReconnectsWithoutReplay() {
        setMode("exit-stream");
        qputenv("AIRCTRL_TEST_WRITE_GATE",temp_.filePath("write.ready").toUtf8());
        Controller c(FAKE_BACKEND); c.setReconnectDelay(200);
        QSignalSpy errors(&c,&Controller::failed), commandErrors(&c,&Controller::commandFailed),
            accepted(&c,&Controller::controlAccepted), status(&c,&Controller::statusReceived);
        c.start(); QTRY_VERIFY(c.observing()); c.setPower(false);
        QTRY_COMPARE(errors.count(),1); QTRY_COMPARE(commandErrors.count(),1); QVERIFY(!c.busy());
        setMode(""); QTRY_COMPARE(c.observationStarts(),quint64(2));
        QTRY_VERIFY(c.observing()); QVERIFY(!c.busy()); QCOMPARE(accepted.count(),0);
        QCOMPARE(status.last()[0].toJsonObject()["pwr"].toString(),QString("1"));
        int writes=0; for(const auto& request:calls()) if(request.contains("set")) ++writes;
        QCOMPARE(writes,1); c.stop();
    }
    void realUdpObservationAndControlUseOneSocketAndSessionKey() {
        UdpDevice device;
        Controller c(REAL_BACKEND); c.configure("127.0.0.1",device.port,5);
        QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        QSignalSpy commandErrors(&c,&Controller::commandFailed), accepted(&c,&Controller::controlAccepted);
        c.start(); QTRY_VERIFY(status.count()>=2);
        QCOMPARE(device.subscriptions.load(),1); QCOMPARE(device.syncs.load(),1); QCOMPARE(device.controls.load(),0);
        c.setPower(false); QTRY_COMPARE(accepted.count(),1); QTRY_VERIFY(!c.busy());
        QCOMPARE(status.last()[0].toJsonObject()["pwr"].toString(),QString("0"));
        QCOMPARE(device.subscriptions.load(),2); QCOMPARE(device.syncs.load(),1); QCOMPARE(device.controls.load(),1);
        QCOMPARE(device.cancellations.load(),1); QCOMPARE(device.changedClientPorts.load(),0);
        QCOMPARE(errors.count(),0); QCOMPARE(commandErrors.count(),0);
        c.stop(); QTRY_COMPARE(device.cancellations.load(),2);
        QCOMPARE(device.changedClientPorts.load(),0); QVERIFY(!device.failed.load());
    }
    void realUdpStatusTimeoutRenewsSocketAndSessionKey() {
        UdpDevice device;
        Controller c(REAL_BACKEND); c.configure("127.0.0.1",device.port,5);
        c.setObservationWatchdogs(1000,250); c.setReconnectDelay(100);
        QSignalSpy status(&c,&Controller::statusReceived), errors(&c,&Controller::failed);
        c.start(); QTRY_VERIFY(status.count()>=1);
        QCOMPARE(device.syncs.load(),1); QCOMPARE(device.changedClientPorts.load(),0);
        device.notificationsEnabled=false;
        QTRY_COMPARE(errors.count(),1);
        QTRY_COMPARE(device.syncs.load(),2); // new Client: close/open plus /sync
        QVERIFY(device.changedClientPorts.load()>0);
        device.notificationsEnabled=true;
        QTRY_VERIFY(status.count()>=2); QTRY_VERIFY(c.observing());
        QCOMPARE(c.observationStarts(),quint64(2));
        c.stop(); QVERIFY(!device.failed.load());
    }
    void twoClientsShareOneServerAndOneDeviceSocket() {
        UdpDevice device;
        Controller first(REAL_BACKEND),second(REAL_BACKEND);
        first.configure("127.0.0.1",device.port,5);
        second.configure("127.0.0.1",device.port,5);
        QSignalSpy firstStatus(&first,&Controller::statusReceived),secondStatus(&second,&Controller::statusReceived);
        QSignalSpy accepted(&first,&Controller::controlAccepted),secondErrors(&second,&Controller::failed);
        first.start(); QTRY_VERIFY(firstStatus.count()>=1);
        second.start(); QTRY_VERIFY(secondStatus.count()>=1);
        QCOMPARE(device.syncs.load(),1);
        QCOMPARE(device.changedClientPorts.load(),0);
        const auto previousSecond=secondStatus.count();
        first.setPower(false); QTRY_COMPARE(accepted.count(),1); QTRY_VERIFY(!first.busy());
        QTRY_VERIFY(secondStatus.count()>previousSecond);
        QCOMPARE(secondStatus.last()[0].toJsonObject()["pwr"].toString(),QString("0"));
        first.stop();
        const auto afterFirstExit=secondStatus.count();
        QTRY_VERIFY(secondStatus.count()>afterFirstExit);
        QCOMPARE(device.syncs.load(),1);
        QCOMPARE(device.changedClientPorts.load(),0);
        QCOMPARE(secondErrors.count(),0);
        second.stop();
        QVERIFY(!device.failed.load());
    }
    void concurrentClientControlsAreSerializedByStatus() {
        UdpDevice device;
        Controller first(REAL_BACKEND),second(REAL_BACKEND);
        first.configure("127.0.0.1",device.port,5);
        second.configure("127.0.0.1",device.port,5);
        QSignalSpy firstStatus(&first,&Controller::statusReceived),secondStatus(&second,&Controller::statusReceived);
        QSignalSpy firstAccepted(&first,&Controller::controlAccepted),secondAccepted(&second,&Controller::controlAccepted);
        // Let the first client establish the one shared server before the
        // second client joins. This test verifies command serialization, not
        // a race between two attempts to launch the same singleton server.
        first.start(); QTRY_VERIFY(firstStatus.count()>=1);
        second.start(); QTRY_VERIFY(secondStatus.count()>=1);
        first.setPower(false); second.setPower(false);
        QTRY_COMPARE(firstAccepted.count(),1); QTRY_COMPARE(secondAccepted.count(),1);
        QTRY_VERIFY(!first.busy()); QTRY_VERIFY(!second.busy());
        QCOMPARE(device.controls.load(),2);
        QCOMPARE(device.controlsWithoutInterveningStatus.load(),0);
        QCOMPARE(device.syncs.load(),1); QCOMPARE(device.changedClientPorts.load(),0);
        first.stop(); second.stop(); QVERIFY(!device.failed.load());
    }
    void powerAlwaysEnabledAndStateColours() {
        Preferences p; p.foreground=Qt::white; p.background=Qt::white;
        Desklet widget(p,FAKE_BACKEND); widget.showAndPosition();
        auto* power=static_cast<PanelButton*>(widget.findChild<QPushButton*>("power"));
        EnablementRecorder recorder; recorder.watch(power);
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
        QImage preview(327,540,QImage::Format_ARGB32_Premultiplied); preview.fill(QColor("#dddddd"));
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
                    painter.drawText(20,i*180+20,names[i]);
                    painter.drawPixmap(20,i*180+28,rendered.grab());
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
        QFETCH(QString,mode); setMode(mode);
        Controller c(FAKE_BACKEND); QSignalSpy errors(&c,&Controller::failed), status(&c,&Controller::statusReceived);
        c.start(); QTRY_COMPARE(errors.count(),1); QCOMPARE(status.count(),0); QVERIFY(!c.observing());
        QCOMPARE(calls().size(),1); c.stop();
    }
    void initialTimeoutAndRecovery() {
        setMode("timeout");
        Controller c(FAKE_BACKEND); c.setObservationWatchdogs(200,1000); c.setReconnectDelay(200);
        QSignalSpy errors(&c,&Controller::failed);
        c.start(); QTRY_COMPARE(errors.count(),1); QVERIFY(!c.observing()); QVERIFY(!c.busy());
        setMode(""); QTRY_VERIFY(c.observing());
        QCOMPARE(calls().size(),2); c.stop();
    }
    void stopCancelsReconnect() {
        setMode("failure");
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
        QFETCH(QString,mode); QFETCH(int,count); setMode(mode);
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
    void receptionAgeThresholdsAndLatch() {
        MonitoringProbe widget(Preferences{},FAKE_BACKEND);
        auto* bar=widget.findChild<MonitorBar*>();
        QSignalSpy raised(&widget,&Desklet::alarmRaised);
        QCOMPARE(widget.dataAgeSeconds(),qint64(-1)); QCOMPARE(bar->ageText(),QString("— s"));
        QCOMPARE(bar->freshness(),DataFreshness::Waiting); QCOMPARE(raised.count(),0);
        widget.applyStatus({{"pwr","1"},{"rh",55}});
        QCOMPARE(bar->ageColor(),QColor("#2ecc71")); QCOMPARE(bar->ageText(),QString("0 s"));
        widget.advance(44); QCOMPARE(bar->freshness(),DataFreshness::Fresh); QCOMPARE(raised.count(),0);
        widget.advance(1); QCOMPARE(bar->ageText(),QString("45 s")); QCOMPARE(bar->ageColor(),QColor("#f1c40f"));
        QCOMPARE(raised.count(),1); QVERIFY(!raised[0][1].toBool());
        widget.advance(44); QCOMPARE(bar->ageText(),QString("89 s")); QCOMPARE(raised.count(),1);
        widget.acknowledgeAlarms(); QVERIFY(bar->alarmText().contains("(Q)"));
        widget.advance(1); QCOMPARE(bar->ageColor(),QColor("#e74c3c")); QCOMPARE(raised.count(),2);
        QVERIFY(raised[1][1].toBool()); QVERIFY(!bar->alarmText().contains("(Q)"));
        widget.setConnectionError("offline"); widget.setConnectionError("offline again");
        widget.advance(500); QCOMPARE(raised.count(),2); QCOMPARE(widget.dataAgeSeconds(),qint64(590));
        widget.applyStatus({{"pwr","1"},{"rh",56}});
        QCOMPARE(bar->ageText(),QString("0 s")); QCOMPARE(bar->alarmText(),QString("Keine Alarme"));
        widget.advance(45); QCOMPARE(raised.count(),3); QCOMPARE(widget.deliveries,3);
    }
    void controlDoesNotResetAgeAndFollowingStatusDoes() {
        qputenv("AIRCTRL_TEST_WRITE_GATE",temp_.filePath("write.ready").toUtf8());
        MonitoringProbe widget(Preferences{},FAKE_BACKEND); widget.start();
        auto* c=widget.findChild<Controller*>(); QSignalSpy packets(c,&Controller::statusPacketReceived);
        QSignalSpy status(c,&Controller::statusReceived);
        QTRY_VERIFY(!status.isEmpty()); widget.findChild<QPushButton*>("power")->click();
        const auto published=status.count(); widget.advance(50);
        const auto before=packets.count(); QTest::qWait(250);
        QCOMPARE(packets.count(),before); QCOMPARE(widget.dataAgeSeconds(),qint64(50));
        QCOMPARE(status.count(),published); QVERIFY(c->busy());
        QCOMPARE(widget.findChild<MonitorBar*>()->freshness(),DataFreshness::Aging);
        QCOMPARE(widget.findChild<Emblem*>("emblem_mode")->icon(),EmblemIcon::Auto);
        QFile gate(temp_.filePath("write.ready")); QVERIFY(gate.open(QIODevice::WriteOnly)); gate.close();
        QTRY_VERIFY(packets.count()>before); QTRY_COMPARE(status.count(),published+1);
        QCOMPARE(widget.dataAgeSeconds(),qint64(0)); QTRY_VERIFY(!c->busy());
        QCOMPARE(widget.findChild<MonitorBar*>()->freshness(),DataFreshness::Fresh);
        c->stop();
    }
    void commandAckAndInvalidJsonDoNotResetAge() {
        MonitoringProbe widget(Preferences{},FAKE_BACKEND); widget.applyStatus({{"pwr","1"}});
        widget.advance(22); auto* c=widget.findChild<Controller*>();
        c->controlAccepted(); QCOMPARE(widget.dataAgeSeconds(),qint64(22));
        c->commandFailed("rejected"); QCOMPARE(widget.dataAgeSeconds(),qint64(22));
        QCOMPARE(widget.findChild<MonitorBar*>()->freshness(),DataFreshness::Fresh);
        QVERIFY(widget.findChild<MonitorBar*>()->alarmText().contains("Fehler"));
        widget.acknowledgeAlarms(); QCOMPARE(widget.findChild<MonitorBar*>()->alarmText(),QString("Keine Alarme"));
        setMode("bad-json");
        QSignalSpy packets(c,&Controller::statusPacketReceived), errors(c,&Controller::failed);
        widget.start(); QTRY_VERIFY(!errors.isEmpty());
        QCOMPARE(packets.count(),0); QCOMPARE(widget.dataAgeSeconds(),qint64(22));
        QCOMPARE(widget.findChild<MonitorBar*>()->freshness(),DataFreshness::Disconnected); c->stop();
    }
    void warningAlarmsDoNotRepeatAndCanRecur() {
        MonitoringProbe widget(Preferences{},FAKE_BACKEND);
        QSignalSpy raised(&widget,&Desklet::alarmRaised);
        QJsonObject status{{"pwr","1"},{"modelid","AC2729/10"},{"func","PH"},{"wl",0},{"fltsts1",88},{"err",49236}};
        widget.applyStatus(status); QCOMPARE(raised.count(),1); QVERIFY(!raised[0][1].toBool());
        QVERIFY(raised[0][0].toString().contains("Filterwechsel")); QVERIFY(raised[0][0].toString().contains("Wasser"));
        QCOMPARE(widget.findChild<MonitorBar*>()->alarmText(),QString("2 Warnungen"));
        for(int i=0;i<5;++i) widget.applyStatus(status);
        QCOMPARE(raised.count(),1); widget.acknowledgeAlarms(); widget.applyStatus(status);
        QCOMPARE(raised.count(),1); QVERIFY(widget.findChild<MonitorBar*>()->alarmText().contains("(Q)"));
        status["wl"]=100; status["fltsts1"]=121; widget.applyStatus(status);
        QCOMPARE(widget.findChild<MonitorBar*>()->alarmText(),QString("Keine Alarme"));
        status["wl"]=0; widget.applyStatus(status); QCOMPARE(raised.count(),2);
    }
    void monitoringSettingsRoundtripCancelAndNoWrites() {
        MonitoringProbe widget(Preferences{},FAKE_BACKEND);
        const auto edit=[](bool accept) {
            QTimer::singleShot(20,[accept] {
                auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if(!dialog) return;
                dialog->findChild<QSpinBox*>("ageWarningSeconds")->setValue(60);
                dialog->findChild<QSpinBox*>("ageStaleSeconds")->setValue(150);
                dialog->findChild<QCheckBox*>("desktopAlarms")->setChecked(false);
                dialog->findChild<QCheckBox*>("alarmSound")->setChecked(true);
                if(accept) dialog->accept(); else dialog->reject();
            });
        };
        edit(false); widget.showAlarmSettings(); QCOMPARE(Preferences::load().ageWarningSeconds,45);
        edit(true); widget.showAlarmSettings(); const auto p=Preferences::load();
        QCOMPARE(p.ageWarningSeconds,60); QCOMPARE(p.ageStaleSeconds,150); QVERIFY(!p.desktopAlarms); QVERIFY(p.alarmSound);
        widget.applyStatus({{"pwr","1"}}); widget.advance(59);
        QCOMPARE(widget.findChild<MonitorBar*>()->freshness(),DataFreshness::Fresh);
        widget.advance(1); QCOMPARE(widget.findChild<MonitorBar*>()->freshness(),DataFreshness::Aging);
        QVERIFY(!QFile::exists(log_));
        QSettings settings; settings.setValue("alarms/warningSeconds",-1); settings.setValue("alarms/staleSeconds",0);
        const auto sanitized=Preferences::load(); QCOMPARE(sanitized.ageWarningSeconds,5); QCOMPARE(sanitized.ageStaleSeconds,6);
    }
    void monitoringAlarmDialogAndContextMenu() {
        MonitoringProbe widget(Preferences{},FAKE_BACKEND); widget.showAndPosition();
        widget.applyStatus({{"pwr","1"}}); widget.advance(45);
        bool opened=false, readable=false;
        QTimer::singleShot(30,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if(!dialog) return;
            opened=dialog->objectName()=="alarmsDialog";
            auto* report=dialog->findChild<QPlainTextEdit*>("activeAlarmReport");
            readable=report && report->toPlainText().contains("45 s");
            dialog->findChild<QPushButton*>("acknowledgeAlarms")->click(); dialog->accept();
        });
        QTest::mouseClick(widget.findChild<MonitorBar*>(),Qt::LeftButton);
        QTRY_VERIFY(opened); QVERIFY(readable);
        QVERIFY(widget.findChild<MonitorBar*>()->alarmText().contains("(Q)"));
        QCOMPARE(clickMenus(widget.findChild<MonitorBar*>(),Qt::RightButton),1); QVERIFY(!QFile::exists(log_));
    }
    void notificationsUseDesktopService() {
        // Run this case under dbus-run-session: never claim/register the real
        // desktop's service or issue test notifications into the user's session.
        if(qEnvironmentVariable("AIRCTRL_TEST_PRIVATE_DBUS")!="1") QSKIP("Private D-Bus integration test needs dbus-run-session");
        auto bus=QDBusConnection::sessionBus(); NotificationProbe service;
        QVERIFY(bus.registerService("org.freedesktop.Notifications"));
        QVERIFY(bus.registerObject("/org/freedesktop/Notifications",&service,QDBusConnection::ExportAllSlots));
        Desklet widget(Preferences{},FAKE_BACKEND);
        widget.setConnectionError("test <error>"); QTRY_COMPARE(service.received,1);
        QVERIFY(service.body.contains("&lt;error&gt;")); QCOMPARE(service.hints.value("urgency").toUInt(),uint(2));
        widget.setConnectionError("test again"); QTest::qWait(50); QCOMPARE(service.received,1);
        Preferences disabled; disabled.desktopAlarms=false;
        Desklet silent(disabled,FAKE_BACKEND); silent.setConnectionError("disabled");
        Desklet demo(Preferences{},FAKE_BACKEND,true); demo.setConnectionError("demo");
        QTest::qWait(50); QCOMPARE(service.received,1);
        bus.unregisterObject("/org/freedesktop/Notifications"); bus.unregisterService("org.freedesktop.Notifications");
    }
    void desktopNotificationRequestIsTyped() {
        for(bool critical:{false,true}) {
            const auto request=alarmNotification("<test> & error",critical);
            QCOMPARE(request.service(),QString("org.freedesktop.Notifications"));
            QCOMPARE(request.path(),QString("/org/freedesktop/Notifications"));
            QCOMPARE(request.interface(),QString("org.freedesktop.Notifications"));
            QCOMPARE(request.member(),QString("Notify"));
            const auto args=request.arguments(); QCOMPARE(args.size(),8);
            QCOMPARE(args[1].metaType().id(),int(QMetaType::UInt));
            QCOMPARE(args[4].toString(),QString("&lt;test&gt; &amp; error"));
            QCOMPARE(args[5].metaType().id(),int(QMetaType::QStringList));
            const auto hints=args[6].toMap(); QCOMPARE(hints["urgency"].metaType().id(),int(QMetaType::UChar));
            QCOMPARE(hints["urgency"].toUInt(),critical ? uint(2) : uint(1));
            QCOMPARE(args[7].toInt(),12000);
        }
    }
    void monitoringPreviewAndDemoSilence() {
        QImage preview(327,720,QImage::Format_ARGB32_Premultiplied); preview.fill(QColor("#dddddd"));
        QPainter painter(&preview); painter.setPen(Qt::black); QFont caption=painter.font(); caption.setPixelSize(13); painter.setFont(caption);
        const QStringList names{"Aktuelle Daten · OK","45 Sekunden · Achtung","90 Sekunden · Daten zu alt","Gerätewarnung · Daten aktuell"};
        for(int i=0;i<4;++i) {
            MonitoringProbe widget(Preferences{},FAKE_BACKEND,true);
            QJsonObject status{{"pwr","1"},{"modelid","AC2729/10"},{"mode","P"},{"func","PH"},
                {"rh",55},{"rhset",50},{"temp",24},{"pm25",1},{"wl",i==3 ? 0 : 100}};
            widget.applyStatus(status); widget.showAndPosition(); widget.advance(i==1 ? 45 : i==2 ? 90 : 3);
            QCoreApplication::processEvents(); QCOMPARE(widget.deliveries,0);
            QCOMPARE(widget.size(),QSize(287,142));
            painter.drawText(20,i*180+20,names[i]); painter.drawPixmap(20,i*180+28,widget.grab());
        }
        painter.end(); const auto path=qEnvironmentVariable("AIRCTRL_TEST_ALARMS_PNG");
        if(!path.isEmpty()) QVERIFY(preview.save(path));
        QVERIFY(!QFile::exists(log_));
    }
    void monitorCirclesScaleAndKeepTheirGeometry() {
        for(int points:{6,10,24,48}) {
            Preferences p; p.valueFont.setPointSize(points); p.transparency=100;
            MonitoringProbe widget(p,FAKE_BACKEND,true); widget.showAndPosition();
            widget.applyStatus({{"pwr","1"},{"mode","P"},{"func","PH"},{"rh",55}});
            auto* bar=widget.findChild<MonitorBar*>(); auto* area=widget.findChild<QWidget*>("statusArea");
            QCOMPARE(bar->parentWidget(),area);
            if(points<=10) QCOMPARE(bar->height(),26);
            QVERIFY(bar->height()<qMax(40,qCeil(QFontMetricsF(p.valueFont).height()*2.4)));
            const auto before=widget.size();
            QCOMPARE(bar->ageCircle().width(),bar->ageCircle().height());
            QCOMPARE(bar->alarmCircle().size(),bar->ageCircle().size());
            QVERIFY(bar->ageCircle().right()<bar->alarmCircle().left());
            QVERIFY(QRectF(bar->rect()).contains(bar->ageCircle()));
            QVERIFY(QRectF(bar->rect()).contains(bar->alarmCircle()));
            QCoreApplication::processEvents();
            auto pixels=widget.grab().toImage();
            QCOMPARE(pixels.pixelColor(bar->mapTo(&widget,QPoint(0,0))).alpha(),0);
            auto sample=[&](const QRectF& circle) {
                return bar->mapTo(&widget,QPoint(qRound(circle.left()+3),qRound(circle.center().y())));
            };
            QCOMPARE(pixels.pixelColor(sample(bar->ageCircle())),QColor("#2ecc71"));
            QCOMPARE(pixels.pixelColor(sample(bar->alarmCircle())),QColor("#e4e4e4"));
            widget.advance(45); pixels=widget.grab().toImage();
            QCOMPARE(pixels.pixelColor(sample(bar->ageCircle())),QColor("#f1c40f"));
            QCOMPARE(pixels.pixelColor(sample(bar->alarmCircle())),QColor("#f1c40f"));
            widget.advance(45); pixels=widget.grab().toImage();
            QCOMPARE(pixels.pixelColor(sample(bar->ageCircle())),QColor("#e74c3c"));
            QCOMPARE(bar->alarmColor(),QColor("#e74c3c"));
            widget.acknowledgeAlarms(); QVERIFY(bar->accessibleName().contains("(Q)"));
            widget.advance(123366); QCOMPARE(bar->ageText(),QString("123456 s"));
            QCOMPARE(widget.size(),before); QVERIFY(widget.rect().contains(QRect(bar->mapTo(&widget,QPoint()),bar->size())));
        }
        QVERIFY(!QFile::exists(log_));
    }
    void diagnosticHexCodes_data() {
        QTest::addColumn<QJsonValue>("value"); QTest::addColumn<QString>("hex");
        QTest::newRow("current-code")<<QJsonValue(49236)<<QString("0xC054");
        QTest::newRow("water")<<QJsonValue(49408)<<QString("0xC100");
        QTest::newRow("clean")<<QJsonValue(49153)<<QString("0xC001");
        QTest::newRow("zero")<<QJsonValue(0)<<QString("0x0000");
        QTest::newRow("decimal-string")<<QJsonValue("0003")<<QString("0x0003");
        QTest::newRow("large")<<QJsonValue(4294967295.0)<<QString("0xFFFFFFFF");
        QTest::newRow("full-uint64-string")<<QJsonValue("18446744073709551615")<<QString("0xFFFFFFFFFFFFFFFF");
        QTest::newRow("overflow")<<QJsonValue("18446744073709551616")<<QString();
        QTest::newRow("negative")<<QJsonValue(-1)<<QString();
        QTest::newRow("fraction")<<QJsonValue(0.5)<<QString();
        QTest::newRow("boolean")<<QJsonValue(false)<<QString();
        QTest::newRow("null")<<QJsonValue(QJsonValue::Null)<<QString();
        QTest::newRow("text")<<QJsonValue("A3")<<QString();
        QTest::newRow("hex-string-not-decimal")<<QJsonValue("0xC054")<<QString();
        QTest::newRow("unsafe-double")<<QJsonValue(9007199254740992.0)<<QString();
    }
    void diagnosticHexCodes() {
        QFETCH(QJsonValue,value); QFETCH(QString,hex);
        for(const auto& tag:{"err","dtrs","ddp","rddp","aqit","aqit_ext","wl"}) {
            const QJsonObject status{{tag,value}};
            const auto fields=describeDeviceFields(status); QCOMPARE(fields.size(),1);
            QCOMPARE(fields.first().hex,hex);
            const auto raw=QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
            QCOMPARE(fields.first().value,QString::fromUtf8(raw.mid(1,raw.size()-2)));
        }
        for(const auto& tag:{"temp","rh","pm25","fltsts0","wicksts","fltt1","unknown"})
            QVERIFY(describeDeviceFields({{tag,value}}).first().hex.isEmpty());
    }
    void decorationPreferenceMigration() {
        QVERIFY(Preferences::load().hideDecoration);
        QSettings settings; settings.setValue("window/desktop",false);
        QVERIFY(!Preferences::load().hideDecoration);
        auto p=Preferences::load(); p.hideDecoration=true; p.save();
        QVERIFY(Preferences::load().hideDecoration); QVERIFY(!Preferences::load().desktop);
        settings.setValue("window/desktop",true); settings.setValue("window/hideDecoration",false);
        QVERIFY(!Preferences::load().hideDecoration); QVERIFY(Preferences::load().desktop);
    }
    void decorationToggleKeepsObservation_data() {
        QTest::addColumn<QString>("session");
        QTest::newRow("x11-routing")<<QString("x11");
        QTest::newRow("wayland-routing")<<QString("wayland");
    }
    void decorationToggleKeepsObservation() {
        QFETCH(QString,session); ScopedEnvironment type("XDG_SESSION_TYPE",session.toUtf8());
        Preferences p; p.desktopAlarms=false; p.position={80,90};
        Desklet widget(p,FAKE_BACKEND); widget.showAndPosition(); widget.start();
        auto* c=widget.findChild<Controller*>(); QTRY_VERIFY(c->statusCount()>0);
        const auto oldPosition=widget.pos(), oldPreference=Preferences::load().position;
        const auto oldSize=widget.size();
        for(bool hidden:{false,true}) {
            bool triggered=false, checked=false;
            QTimer::singleShot(30,[&] {
                auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget()); if(!menu) return;
                auto* action=menu->findChild<QAction*>("hideWindowDecoration");
                if(action) { checked=action->isChecked(); action->trigger(); triggered=true; }
                menu->close();
            });
            QTest::mouseClick(widget.findChild<MonitorBar*>(),Qt::RightButton);
            QTRY_VERIFY(triggered); QCOMPARE(checked,!hidden);
            QTRY_COMPARE(widget.windowFlags().testFlag(Qt::FramelessWindowHint),hidden);
            QVERIFY(widget.isVisible()); QCOMPARE(widget.size(),oldSize);
            QCOMPARE(Preferences::load().hideDecoration,hidden);
            QCOMPARE(c->observationStarts(),quint64(1)); QCOMPARE(calls().size(),1);
            QVERIFY(c->observing()); QVERIFY(widget.findChild<QPushButton*>("power")->isEnabled());
            if(session=="x11") QCOMPARE(widget.pos(),oldPosition);
            else QCOMPARE(Preferences::load().position,p.position);
            if(QGuiApplication::platformName()=="xcb" && session=="x11") {
                QCOMPARE(widget.testAttribute(Qt::WA_X11NetWmWindowTypeDock),hidden);
                QVERIFY(widget.windowFlags().testFlag(Qt::WindowStaysOnBottomHint));
            }
        }
        Q_UNUSED(oldPreference);
        c->stop();
    }
    void decorationDemoDoesNotPersist() {
        Desklet widget(Preferences{},FAKE_BACKEND,true); widget.showAndPosition();
        bool triggered=false;
        QTimer::singleShot(30,[&] {
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget()); if(!menu) return;
            if(auto* action=menu->findChild<QAction*>("hideWindowDecoration")) { action->trigger(); triggered=true; }
            menu->close();
        });
        QTest::mouseClick(widget.findChild<MonitorBar*>(),Qt::RightButton);
        QTRY_VERIFY(triggered); QTRY_VERIFY(!widget.windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(Preferences::load().hideDecoration); QVERIFY(!QFile::exists(log_));
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
            {"wl",100},{"err",49236},{"fltsts0",357},{"fltsts1",121},{"fltsts2",121},{"wicksts",121},
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
        QVERIFY(!emblemIds(status).contains("clean")); QVERIFY(emblemIds(status).contains("filter"));
        status["modelid"]="AC9999/10";
        QCOMPARE(emblemIds(status),QStringList({"function","wifi"}));
        status.remove("modelid"); status["type"]="AC2729";
        QVERIFY(emblemIds(status).contains("filter"));
    }
    void filterWarningBoundaries_data() {
        QTest::addColumn<QJsonValue>("hours"); QTest::addColumn<int>("level");
        QTest::newRow("above-threshold")<<QJsonValue(121)<<int(AlertLevel::None);
        QTest::newRow("local-threshold")<<QJsonValue(120)<<int(AlertLevel::Warning);
        QTest::newRow("earlier-user-snapshot")<<QJsonValue(119)<<int(AlertLevel::Warning);
        QTest::newRow("current-user-snapshot")<<QJsonValue(88)<<int(AlertLevel::Warning);
        QTest::newRow("last-hour")<<QJsonValue(1)<<int(AlertLevel::Warning);
        QTest::newRow("expired")<<QJsonValue(0)<<int(AlertLevel::Error);
        QTest::newRow("numeric-string")<<QJsonValue("88")<<int(AlertLevel::Warning);
        QTest::newRow("expired-string")<<QJsonValue("0")<<int(AlertLevel::Error);
        QTest::newRow("missing")<<QJsonValue(QJsonValue::Undefined)<<int(AlertLevel::None);
        QTest::newRow("null")<<QJsonValue()<<int(AlertLevel::None);
        QTest::newRow("bool")<<QJsonValue(false)<<int(AlertLevel::None);
        QTest::newRow("negative")<<QJsonValue(-1)<<int(AlertLevel::None);
        QTest::newRow("fraction")<<QJsonValue(0.5)<<int(AlertLevel::None);
        QTest::newRow("invalid-string")<<QJsonValue("bad")<<int(AlertLevel::None);
    }
    void filterWarningBoundaries() {
        QFETCH(QJsonValue,hours); QFETCH(int,level);
        const QStringList tags{"fltsts1","fltsts2","wicksts"}, codes{"A3","C7","F1"};
        for(int i=0;i<tags.size();++i) {
            QJsonObject status{{"pwr","1"},{"modelid","AC2729/10"},{"func","PH"},{"err",0}};
            status[tags[i]]=hours;
            const auto alerts=deviceAlerts(status);
            QCOMPARE(alerts.size(),level==int(AlertLevel::None) ? 0 : 1);
            QVERIFY(!emblemIds(status).contains("clean"));
            if(alerts.isEmpty()) continue;
            QCOMPARE(int(alerts[0].level),level);
            QVERIFY(alerts[0].message.contains(tags[i])); QVERIFY(alerts[0].message.contains(codes[i]));
            QVERIFY(emblemIds(status).contains("filter"));
            if(level==int(AlertLevel::Warning)) QVERIFY(alerts[0].message.contains("lokale Vorwarngrenze"));
            status["pwr"]="0"; QVERIFY(deviceAlerts(status).isEmpty());
            status["pwr"]="1"; status["modelid"]="AC9999/10"; QVERIFY(deviceAlerts(status).isEmpty());
        }
    }
    void observedFilterWarningsAndIndependentEscalation() {
        MonitoringProbe widget(Preferences{},FAKE_BACKEND);
        widget.showAndPosition();
        QSignalSpy raised(&widget,&Desklet::alarmRaised);
        QJsonObject status{{"pwr","1"},{"modelid","AC2729/10"},{"func","PH"},
            {"err",49236},{"wl",100},{"fltsts0",326},{"fltsts1",88},{"fltsts2",88},{"wicksts",88},
            {"fltt1","A3"},{"fltt2","C7"},{"mode","P"},{"rh",67},{"rhset",50},{"temp",24},{"pm25",2}};
        auto* bar=widget.findChild<MonitorBar*>();
        widget.applyStatus(status); QCOMPARE(raised.count(),1);
        QCOMPARE(deviceAlerts(status).size(),3); QCOMPARE(bar->alarmText(),QString("3 Warnungen"));
        QCOMPARE(bar->alarmColor(),QColor("#f1c40f"));
        QCoreApplication::processEvents(); // lay out newly visible emblems before rendering
        const auto preview=qEnvironmentVariable("AIRCTRL_TEST_FILTERS_PNG");
        if(!preview.isEmpty()) QVERIFY(widget.grab().save(preview));
        for(const auto& code:{"A3","C7","F1"}) QVERIFY(raised[0][0].toString().contains(code));
        QVERIFY(!emblemIds(status).contains("clean"));
        widget.acknowledgeAlarms();
        for(const auto& tag:{"fltsts1","fltsts2","wicksts"}) status[tag]=87;
        widget.applyStatus(status); QCOMPARE(raised.count(),1); QVERIFY(bar->alarmText().contains("(Q)"));
        status["wicksts"]=0; widget.applyStatus(status);
        QCOMPARE(raised.count(),2); QVERIFY(raised[1][1].toBool());
        QVERIFY(raised[1][0].toString().contains("F1")); QCOMPARE(bar->alarmColor(),QColor("#e74c3c"));
        QVERIFY(!bar->alarmText().contains("(Q)")); QVERIFY(!emblemIds(status).contains("clean"));
        widget.acknowledgeAlarms(); widget.applyStatus(status); QCOMPARE(raised.count(),2);
        status["fltsts1"]=0; widget.applyStatus(status); QCOMPARE(raised.count(),3);
        QVERIFY(raised[2][0].toString().contains("A3"));
        for(const auto& tag:{"fltsts1","fltsts2","wicksts"}) status[tag]=2000;
        widget.applyStatus(status); QCOMPARE(bar->alarmText(),QString("Keine Alarme"));
        // err and the installed filter identifiers by themselves are no alarm.
        QVERIFY(deviceAlerts(status).isEmpty());
        status["wicksts"]=119; widget.applyStatus(status); QCOMPARE(raised.count(),4);
        QVERIFY(raised[3][0].toString().contains("F1")); QVERIFY(!raised[3][1].toBool());
        QVERIFY(!QFile::exists(log_));
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
        auto* statusArea=widget.findChild<QWidget*>("statusArea");
        QVERIFY(widget.findChild<QWidget*>("controlBar")->geometry().bottom()<statusArea->geometry().top());
        QVERIFY(statusArea->geometry().bottom()<widget.findChild<QWidget*>("values")->geometry().top());
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
        QImage preview(347,samples.size()*190,QImage::Format_ARGB32_Premultiplied); preview.fill(QColor("#dddddd"));
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
                    QCOMPARE(widget.width(),287); QCOMPARE(widget.height(),142);
                    painter.drawText(20,i*190+20,names[i]); painter.drawPixmap(20,i*190+28,widget.grab());
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
        QString powerDescription,errorDescription,errorHex,unknownDescription,unknownValue,rawText,copied;
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
                    if(tag=="pwr") powerDescription=device->item(row,3)->text();
                    if(tag=="err") { errorDescription=device->item(row,3)->text(); errorHex=device->item(row,2)->text(); }
                    if(tag=="future_tag") {
                        unknownValue=device->item(row,1)->text();
                        unknownDescription=device->item(row,3)->text();
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
        QCOMPARE(errorHex,QString("0xC054")); QVERIFY(copied.contains("err = 49236 [0xC054]"));
        QCOMPARE(unknownValue,QString("{\"x\":1}"));
        QVERIFY(unknownDescription.contains("Nicht dokumentiertes"));
        QVERIFY(rawText.contains("\"future_tag\"")); QVERIFY(rawText.contains("49236"));
        QVERIFY(copied.contains("ERKLÄRTE GERÄTEWERTE")); QVERIFY(copied.contains("UNVERÄNDERTE ROHDATEN"));
        if(qEnvironmentVariableIsSet("AIRCTRL_TEST_DIAGNOSTICS_PNG")) QVERIFY(screenshotSaved);
        QVERIFY(!QFile::exists(log_));
    }
    void diagnosticCopyButtonAndShortcut() {
        Desklet widget(Preferences{},FAKE_BACKEND,true); widget.showAndPosition();
        widget.applyStatus({{"pwr","1"},{"modelid","AC2729/10"},{"err",49236},{"wicksts",88},
            {"name","Wohnzimmer · Jürgen"}});
        bool inspected=false;
        QString copied, expected, feedback, shortcutCopy, selectionCopy;
        auto* clipboard=QApplication::clipboard(); clipboard->setText("old clipboard");
        const bool selection=clipboard->supportsSelection();
        if(selection) clipboard->setText("old selection",QClipboard::Selection);
        QTimer::singleShot(80,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog) return;
            auto* copy=dialog->findChild<QPushButton*>("copyDiagnosticReport");
            auto* report=dialog->findChild<QPlainTextEdit*>("diagnosticReport");
            auto* status=dialog->findChild<QLabel*>("reportCopyStatus");
            if(copy && report && status) {
                expected=report->toPlainText();
                QTest::mouseClick(copy,Qt::LeftButton);
                copied=clipboard->text(QClipboard::Clipboard); feedback=status->text();
                if(selection) selectionCopy=clipboard->text(QClipboard::Selection);
                inspected=dialog->isVisible() && !dialog->windowFlags().testFlag(Qt::FramelessWindowHint);
                clipboard->setText("replace before shortcut");
                copy->setFocus(); QTest::keyClick(copy,Qt::Key_C,Qt::ControlModifier|Qt::ShiftModifier);
                QTest::qWait(150); // QAbstractButton shortcuts use animateClick().
                shortcutCopy=clipboard->text(QClipboard::Clipboard);
            }
            dialog->accept();
        });
        widget.showDetails();
        QVERIFY(inspected); QVERIFY(!expected.isEmpty()); QCOMPARE(copied,expected); QCOMPARE(shortcutCopy,expected);
        if(selection) QCOMPARE(selectionCopy,expected);
        QVERIFY(feedback.contains("Bericht kopiert")); QVERIFY(feedback.contains("Strg+Umschalt+V"));
        QVERIFY(copied.contains("Jürgen")); QVERIFY(copied.contains("err = 49236 [0xC054]"));
        QVERIFY(copied.contains("F1")); QVERIFY(copied.contains("ERKLÄRTE GERÄTEWERTE"));
        QCOMPARE(clipboard->text(QClipboard::Clipboard),expected); // survives closing the dialog
        QVERIFY(!QFile::exists(log_));
    }
    void diagnosticCopyAfterContextMenu() {
        Desklet widget(Preferences{},FAKE_BACKEND,true); widget.showAndPosition();
        bool selected=false, inspected=false, popupGone=false, copied=false;
        QTimer inspector;
        connect(&inspector,&QTimer::timeout,this,[&] {
            if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                if(selected) return;
                for(auto* action:menu->actions()) if(action->text().startsWith("Diagnose /")) {
                    selected=true;
                    QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());
                    break;
                }
            } else if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                inspector.stop();
                popupGone=QApplication::activePopupWidget()==nullptr;
                if(auto* copy=dialog->findChild<QPushButton*>("copyDiagnosticReport")) {
                    QTest::mouseClick(copy,Qt::LeftButton);
                    copied=QApplication::clipboard()->text().contains("ERKLÄRTE GERÄTEWERTE");
                }
                inspected=true; dialog->accept();
            }
        });
        inspector.start(25);
        // Ensure a failed popup transition cannot leave the test stuck in exec().
        QTimer watchdog; watchdog.setSingleShot(true);
        connect(&watchdog,&QTimer::timeout,this,[] {
            if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
            if(auto* popup=QApplication::activePopupWidget()) popup->close();
        });
        watchdog.start(1500);
        QTest::mouseClick(&widget,Qt::RightButton,Qt::NoModifier,QPoint(3,3));
        QTRY_VERIFY_WITH_TIMEOUT(inspected,2000);
        QVERIFY(selected); QVERIFY(popupGone); QVERIFY(copied); QVERIFY(!QFile::exists(log_));
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
            if(points==10) { QVERIFY(widget.width()<=340); QVERIFY(widget.height()<155); }
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
            const int expectedAlpha=qRound((100-transparency)*2.55);
            const QList<QPoint> backgroundPoints{{0,0},{image.width()-1,0},
                {0,image.height()-1},{image.width()-1,image.height()-1},
                {image.width()/2,image.height()-3}};
            for(const auto& point:backgroundPoints)
                QVERIFY(qAbs(image.pixelColor(point).alpha()-expectedAlpha)<=1);
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
    void leftClickNeverOpensContextMenu() {
        for(const bool locked:{false,true}) {
            Preferences p; p.locked=locked;
            Desklet widget(p,FAKE_BACKEND); widget.showAndPosition();
            const auto position=widget.pos();
            QCOMPARE(clickMenus(widget.findChild<QLabel*>("value_rh"),Qt::LeftButton),0);
            QCOMPARE(clickMenus(widget.findChild<Emblem*>("emblem_wifi"),Qt::LeftButton),0);
            QCOMPARE(widget.pos(),position); QVERIFY(!QFile::exists(log_));
        }
    }
    void draggingMovesAndSavesWithoutOpeningMenu_data() {
        QTest::addColumn<QString>("surface");
        QTest::newRow("value")<<QString("value_rh");
        QTest::newRow("emblem")<<QString("emblem_wifi");
        QTest::newRow("monitor")<<QString("monitorBar");
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
        QTest::mouseClick(widget.findChild<QLabel*>("value_rh"),Qt::RightButton);
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
        QCOMPARE(menus,0);
        QTest::mouseClick(value,Qt::RightButton); QTest::qWait(90);
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
        p.desktop = false; p.hideDecoration=false; p.locked = true; p.position = {42,60};
        p.background=QColor("#334455"); p.foreground=QColor("#ddccbb"); p.transparency=65;
        p.valueFont=QFont("DejaVu Serif",22,QFont::Bold,true); p.visibleValues={"rh","temp","iaql"}; p.save();
        auto q = Preferences::load(); QCOMPARE(q.host,p.host); QCOMPARE(q.port,p.port);
        QCOMPARE(q.interval,p.interval); QCOMPARE(q.position,p.position); QVERIFY(q.locked); QVERIFY(!q.desktop);
        QVERIFY(!q.hideDecoration);
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
