#pragma once

#include "alerts.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>

struct lua_State;
struct lua_Debug;

class AutomationEngine : public QObject {
    Q_OBJECT
public:
    explicit AutomationEngine(bool persistent = true, QObject* parent = nullptr);
    ~AutomationEngine() override;

    bool enabled() const { return enabled_; }
    bool loaded() const { return state_ != nullptr && lastError_.isEmpty(); }
    QString lastError() const { return lastError_; }
    QString lastEvent() const { return lastEvent_; }
    QString lastAction() const { return lastAction_; }
    int scheduleCount() const { return schedules_.size(); }
    QStringList logEntries() const { return logEntries_; }

    static QString scriptPath();
    static QString exampleScript();
    static QString luaRelease();

    void setEnabled(bool enabled);
    bool reload();
    bool loadScriptText(const QString& text, const QString& sourceName = "automation.lua");
    bool saveScript(const QString& text, QString* error = nullptr);
    void setConnected(bool connected, const QString& reason = {},
                      const QDateTime& now = QDateTime::currentDateTime());
    void statusEvent(const QJsonObject& status,
                     const QDateTime& now = QDateTime::currentDateTime());
    void alertsEvent(const QList<Alert>& alerts);
    void commandEvent(const QString& source, bool ok, const QString& message);
    void processTime(const QDateTime& now = QDateTime::currentDateTime());
    void actionAccepted(const QString& occurrenceKey);
    QString diagnostics() const;

signals:
    void actionRequested(QJsonObject values, QString source, QString occurrenceKey);
    void problemChanged(QString problem);
    void logMessage(QString message);

private:
    struct Schedule {
        QString name;
        QTime at;
        QSet<int> days;
        QJsonObject values;
        bool catchUp = true;
    };
    struct PendingAction {
        QJsonObject values;
        QString source;
        QString occurrenceKey;
    };
    struct MemoryLimit {
        size_t used = 0;
        size_t maximum = 8 * 1024 * 1024;
    };

    static void* allocator(void* userData, void* pointer, size_t oldSize, size_t newSize);
    static void instructionHook(lua_State* state, lua_Debug* debug);
    static AutomationEngine* fromLua(lua_State* state);
    static int luaLog(lua_State* state);
    static int luaSet(lua_State* state);
    static int luaSchedule(lua_State* state);
    static int luaStatus(lua_State* state);

    void closeState();
    void openSandbox();
    void registerApi();
    void setProblem(const QString& problem);
    void appendLog(const QString& level, const QString& message);
    bool callEvent(const QString& type, const QJsonObject& detail = {});
    void flushActions();
    void evaluateSchedules(const QDateTime& now);
    QString occurrenceFor(const Schedule& schedule, const QDateTime& now, QDateTime* when = nullptr) const;
    bool occurrenceHandled(const QString& key) const;
    void rememberOccurrence(const QString& key);

    lua_State* state_ = nullptr;
    MemoryLimit memory_;
    bool persistent_ = true;
    bool enabled_ = false;
    bool connected_ = false;
    bool dispatching_ = false;
    int instructionBudget_ = 200000;
    QString currentEvent_;
    QString lastError_;
    QString lastEvent_;
    QString lastAction_;
    QString lastMinute_;
    QString lastAlertDigest_;
    QJsonObject latestStatus_;
    QList<Schedule> schedules_;
    QList<PendingAction> pendingActions_;
    QStringList handledOccurrences_;
    QStringList logEntries_;
};
