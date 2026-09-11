#include "automation.hpp"
#include "controlvalues.hpp"

#include <QFile>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

class AutomationTests : public QObject {
    Q_OBJECT
private slots:
    void exampleAndSandboxLoad() {
        QFile example(AUTOMATION_EXAMPLE_FILE);
        QVERIFY(example.open(QIODevice::ReadOnly));
        const QString text=AutomationEngine::exampleScript();
        QCOMPARE(text.toUtf8(),example.readAll());
        for(const QString& event:QStringList{"startup","time","connected","disconnected","status","alarm","command"})
            QVERIFY2(text.contains("-- "+event),qPrintable("Ereignis fehlt in der Beispielreferenz: "+event));
        for(const QString& field:QStringList{"pwr","cl","mode","om","func","uil","rhset","aqil","dt"})
            QVERIFY2(text.contains("-- "+field),qPrintable("Steuerwert fehlt in der Beispielreferenz: "+field));
        AutomationEngine engine(false);
        QVERIFY2(engine.loadScriptText(text),qPrintable(engine.lastError()));
        QCOMPARE(engine.scheduleCount(),2);
        QVERIFY(engine.logEntries().isEmpty());
        QVERIFY(engine.diagnostics().contains("ohne io, os, package, debug"));
        QCOMPARE(AutomationEngine::luaRelease(),QString("Lua 5.4.9"));
    }

    void sandboxHidesHostAccess() {
        AutomationEngine engine(false);
        const QString script=QStringLiteral(R"lua(
            assert(io == nil and os == nil and package == nil and debug == nil)
            assert(dofile == nil and loadfile == nil and load == nil and require == nil)
            airctrl.log("info", "sandbox-ok")
        )lua");
        QVERIFY2(engine.loadScriptText(script),qPrintable(engine.lastError()));
        QVERIFY(engine.logEntries().join('\n').contains("sandbox-ok"));
    }

    void dayNightScheduleRunsOncePerOccurrence() {
        AutomationEngine engine(false); QSignalSpy actions(&engine,&AutomationEngine::actionRequested);
        QVERIFY(engine.loadScriptText(R"lua(
            airctrl.schedule { name="nacht", at="22:00", set={mode="S", om="s", uil="0"} }
            airctrl.schedule { name="tag", at="07:00", set={mode="P", uil="1"} }
        )lua"));
        const QDateTime night(QDate(2026,9,7),QTime(22,1));
        engine.setConnected(true,{},night); QCOMPARE(actions.size(),1);
        const QList<QVariant> first=actions.takeFirst();
        QCOMPARE(first[0].toJsonObject(),QJsonObject({{"mode","S"},{"om","s"},{"uil","0"}}));
        QVERIFY(first[1].toString().contains("nacht")); QVERIFY(!first[2].toString().isEmpty());
        engine.actionAccepted(first[2].toString());
        engine.processTime(night.addSecs(60)); QCOMPARE(actions.size(),0);
        const QDateTime morning=QDateTime(QDate(2026,9,8),QTime(7,0));
        engine.processTime(morning); QCOMPARE(actions.size(),1);
        QCOMPARE(actions.first()[0].toJsonObject(),QJsonObject({{"mode","P"},{"uil","1"}}));
    }

    void catchUpCanBeDisabledAndWeekdaysApply() {
        AutomationEngine engine(false); QSignalSpy actions(&engine,&AutomationEngine::actionRequested);
        QVERIFY(engine.loadScriptText(R"lua(
            airctrl.schedule { name="werktag", at="07:00", days={1,2,3,4,5}, catch_up=false,
                               set={mode="P"} }
        )lua"));
        const QDateTime sunday(QDate(2026,9,6),QTime(7,0));
        engine.setConnected(true,{},sunday); QCOMPARE(actions.size(),0);
        engine.processTime(QDateTime(QDate(2026,9,7),QTime(7,1))); QCOMPARE(actions.size(),0);
        engine.processTime(QDateTime(QDate(2026,9,8),QTime(7,0))); QCOMPARE(actions.size(),1);
    }

    void statusEventContainsChangedValues() {
        AutomationEngine engine(false); QSignalSpy actions(&engine,&AutomationEngine::actionRequested);
        QVERIFY(engine.loadScriptText(R"lua(
            function on_event(event)
                if event.type == "status" and event.changed.rh and event.status.rh < 35 then
                    airctrl.set { func="PH" }
                end
            end
        )lua"));
        const QDateTime now(QDate(2026,9,7),QTime(12,0));
        engine.statusEvent({{"rh",34},{"func","P"}},now); QCOMPARE(actions.size(),1);
        QCOMPARE(actions.first()[0].toJsonObject(),QJsonObject({{"func","PH"}})); actions.clear();
        engine.statusEvent({{"rh",34},{"func","P"}},now.addSecs(60)); QCOMPARE(actions.size(),0);
        engine.statusEvent({{"rh",33},{"func","P"}},now.addSecs(120)); QCOMPARE(actions.size(),1);
    }

    void alarmAndConnectionEventsArrive() {
        AutomationEngine engine(false);
        QVERIFY(engine.loadScriptText(R"lua(
            function on_event(event)
                if event.type == "connected" then airctrl.log("info", "online") end
                if event.type == "disconnected" then airctrl.log("warning", event.reason) end
                if event.type == "alarm" then airctrl.log("info", event.highest .. ":" .. event.count) end
            end
        )lua"));
        engine.setConnected(true); engine.alertsEvent({{"water",AlertLevel::Warning,"Wasser"}});
        engine.setConnected(false,"Testfehler");
        const QString log=engine.logEntries().join('\n');
        QVERIFY(log.contains("online")); QVERIFY(log.contains("warning:1")); QVERIFY(log.contains("Testfehler"));
    }

    void unchangedAlarmDoesNotRepeatWhenMessageAgeChanges() {
        AutomationEngine engine(false);
        QVERIFY(engine.loadScriptText(R"lua(
            function on_event(event)
                if event.type == "alarm" then airctrl.log("warning", event.alerts[1].message) end
            end
        )lua"));
        engine.alertsEvent({{"connection",AlertLevel::Warning,"Keine Daten seit 45 Sekunden"}});
        QCOMPARE(engine.logEntries().size(),1);
        engine.alertsEvent({{"connection",AlertLevel::Warning,"Keine Daten seit 46 Sekunden"}});
        QCOMPARE(engine.logEntries().size(),1);
        engine.alertsEvent({{"connection",AlertLevel::Error,"Keine Daten seit 90 Sekunden"}});
        QCOMPARE(engine.logEntries().size(),2);
    }

    void invalidControlAndScheduleAreRejected() {
        AutomationEngine invalid(false);
        QVERIFY(!invalid.loadScriptText("airctrl.schedule{name='x',at='7:00',set={evil=1}}"));
        QVERIFY(invalid.lastError().contains("HH:MM"));
        QCOMPARE(controlValuesError({{"evil",1}}),QString("Ungültiger Steuerwert: evil"));
        QVERIFY(!controlValuesError({{"mode","S"},{"aqil",25}}).isEmpty());
        AutomationEngine overlap(false);
        QVERIFY(!overlap.loadScriptText(R"lua(
            airctrl.schedule{name="a",at="07:00",days={1},set={mode="P"}}
            airctrl.schedule{name="b",at="07:00",days={1,2},set={mode="S"}}
        )lua"));
        QVERIFY(overlap.lastError().contains("überschneiden"));
        AutomationEngine wrongCatchUp(false);
        QVERIFY(!wrongCatchUp.loadScriptText(
            "airctrl.schedule{name='x',at='07:00',catch_up='no',set={mode='P'}}"));
        QVERIFY(wrongCatchUp.lastError().contains("Boolean"));
        AutomationEngine tooMany(false);
        QVERIFY(!tooMany.loadScriptText(R"lua(
            for i=1,65 do
                airctrl.schedule {
                    name="s" .. i,
                    at=string.format("%02d:%02d", math.floor((i-1)/60), (i-1)%60),
                    set={mode="P"}
                }
            end
        )lua"));
        QVERIFY(tooMany.lastError().contains("64 Zeitpläne"));
    }

    void instructionLimitStopsRunawayScript() {
        AutomationEngine engine(false);
        QVERIFY(!engine.loadScriptText(R"lua(
            function on_event(event)
                if event.type == "time" then while true do end end
            end
        )lua"));
        QVERIFY(engine.lastError().contains("Ausführungslimit"));
    }

    void memoryLimitStopsOversizedState() {
        AutomationEngine engine(false);
        QVERIFY(!engine.loadScriptText(R"lua(
            local values = {}
            for i=1,20 do values[i] = string.rep("x", 1024 * 1024) end
        )lua"));
        QVERIFY(engine.lastError().contains("memory",Qt::CaseInsensitive));
    }

    void setIsNotAllowedFromCommandFeedback() {
        AutomationEngine engine(false); QSignalSpy actions(&engine,&AutomationEngine::actionRequested);
        QVERIFY(engine.loadScriptText(R"lua(
            function on_event(event)
                if event.type == "command" then airctrl.set { pwr="1" } end
            end
        )lua"));
        engine.commandEvent("test",false,"failure");
        QCOMPARE(actions.size(),0); QVERIFY(engine.lastError().contains("nur in connected"));
    }
};

QTEST_GUILESS_MAIN(AutomationTests)
#include "test_automation.moc"
