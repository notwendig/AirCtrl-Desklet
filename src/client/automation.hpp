/**
 * @file automation.hpp
 * @brief Sandboxed Lua event and schedule engine for confirmed device state.
 */
#pragma once

#include "alerts.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>

struct lua_State;
struct lua_Debug;

/**
 * @brief Owns one resource-limited Lua state and dispatches desklet events.
 *
 * The sandbox has no general file, process, package, or network API. Device
 * writes leave the engine through actionRequested() and therefore use the same
 * validated TCP client path and confirmation rules as panel clicks.
 */
class AutomationEngine : public QObject {
    Q_OBJECT
public:
    /** @brief Construct an engine with optional persistence of user state. */
    explicit AutomationEngine(bool persistent = true, QObject* parent = nullptr);
    ~AutomationEngine() override;

    bool enabled() const { return enabled_; }
    bool loaded() const { return state_ != nullptr && lastError_.isEmpty(); }
    QString lastError() const { return lastError_; }
    QString lastEvent() const { return lastEvent_; }
    QString lastAction() const { return lastAction_; }
    int scheduleCount() const { return schedules_.size(); }
    QStringList logEntries() const { return logEntries_; }

    /** @brief Return the per-user automation script path. */
    static QString scriptPath();
    /** @brief Return the canonical embedded example script. */
    static QString exampleScript();
    /** @brief Return the embedded Lua release string. */
    static QString luaRelease();

    /** @brief Enable or disable event execution. */
    void setEnabled(bool enabled);
    /** @brief Reload the configured script into a fresh sandbox. */
    bool reload();
    /** @brief Load script text into a fresh sandbox without file access. */
    bool loadScriptText(const QString& text, const QString& sourceName = "automation.lua");
    /** @brief Persist script text after validating size and syntax. */
    bool saveScript(const QString& text, QString* error = nullptr);
    /** @brief Publish a server/device connection transition to Lua. */
    void setConnected(bool connected, const QString& reason = {},
                      const QDateTime& now = QDateTime::currentDateTime());
    /** @brief Publish a new confirmed device snapshot and its changed fields. */
    void statusEvent(const QJsonObject& status,
                     const QDateTime& now = QDateTime::currentDateTime());
    /** @brief Publish stable alarm identities and severities. */
    void alertsEvent(const QList<Alert>& alerts);
    /** @brief Publish the result of an automation-originated command. */
    void commandEvent(const QString& source, bool ok, const QString& message);
    /** @brief Evaluate minute and schedule events at the supplied local time. */
    void processTime(const QDateTime& now = QDateTime::currentDateTime());
    /** @brief Persist the occurrence key of an accepted scheduled action. */
    void actionAccepted(const QString& occurrenceKey);
    /** @brief Return a human-readable engine state for diagnostics. */
    QString diagnostics() const;

signals:
    /** @brief Requests a validated device action from the owning desklet. */
    void actionRequested(QJsonObject values, QString source, QString occurrenceKey);
    /** @brief Reports a load or runtime problem to the UI. */
    void problemChanged(QString problem);
    /** @brief Appends one localized entry to the visible automation log. */
    void logMessage(QString message);

private:
    struct Schedule {
        QString name;
        QTime at;
        QTime until;
        QSet<int> days;
        QJsonObject conditions;
        QJsonObject values;
        bool hasWindow = false;
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
    bool conditionsMatch(const Schedule& schedule) const;
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
