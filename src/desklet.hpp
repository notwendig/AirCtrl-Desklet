/**
 * @file desklet.hpp
 * @brief Main Qt widget coordinating presentation, TCP control, and automation.
 */
#pragma once
#include "controller.hpp"
#include "preferences.hpp"
#include "panel.hpp"
#include "emblems.hpp"
#include "alerts.hpp"
#include "automation.hpp"
#include <QElapsedTimer>
#include <QDateTime>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QWidget>
#include <QGridLayout>
#include <array>

/**
 * @brief User-facing desktop panel for one AirControl server endpoint.
 *
 * The widget consumes only confirmed snapshots supplied by Controller. It has
 * no Philips protocol or UDP transport and never starts the production server.
 */
class Desklet : public QWidget {
    Q_OBJECT
public:
    /** @brief Construct the widget from validated user preferences. */
    Desklet(Preferences preferences, QString backend, bool demo = false);
    /** @brief Start client connection, monitoring, and enabled automation. */
    void start();
    /** @brief Open the server-connection and autostart settings dialog. */
    void showSettings();
    /** @brief Open the diagnostic status and raw-data dialog. */
    void showDetails();
    /** @brief Open the current alarm details dialog. */
    void showAlarms();
    /** @brief Open freshness and desktop-notification settings. */
    void showAlarmSettings();
    /** @brief Open the Lua automation editor and event log. */
    void showAutomationSettings();
    /** @brief Acknowledge all currently active alarm identities. */
    void acknowledgeAlarms();
    /** @brief Return monotonic seconds since the last valid status packet. */
    qint64 dataAgeSeconds() const;
    /** @brief Apply one confirmed status snapshot to all views and automation. */
    void applyStatus(const QJsonObject& status);
    /** @brief Mark reception failed while retaining visibly stale values. */
    void setConnectionError(const QString& error);
    /** @brief Show the widget and restore its valid desktop position. */
    void showAndPosition();
signals:
    /** @brief Requests delivery of one latched desktop alarm. */
    void alarmRaised(QString message, bool critical);
protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void closeEvent(QCloseEvent*) override;
    virtual bool startNativeMove();
    virtual qint64 monotonicMs() const;
    virtual void deliverAlarm(const QString& message, bool critical);
    void updateMonitoring();
private:
    void recordReception();
    void updateControls();
    void updateFooter();
    void updateValues();
    void updateEmblems();
    void applyAppearance();
    void saveAppearance();
    void resizeToContent();
    void applyWindowMode();
    void setDecorationHidden(bool hidden);
    void requestMenu(const QPoint& point);
    void openMenu(const QPoint& point);
    void showPositionDialog();
    void openControl(int index);
    void sendValues(const QJsonObject& values);
    void sendAutomationValues(const QJsonObject& values, const QString& source, const QString& occurrenceKey);
    void rememberPosition();
    Preferences preferences_;
    Controller controller_;
    AutomationEngine automation_;
    bool demo_ = false;
    bool waylandSession_ = false;
    bool connected_ = false;
    bool awaitingConfirmation_ = false;
    bool pendingAutomation_ = false;
    bool leftPressed_ = false;
    bool rightPressed_ = false;
    bool mouseMoved_ = false;
    bool menuPending_ = false;
    bool menuOpen_ = false;
    QPoint pressPosition_;
    QPoint dragOffset_;
    QDateTime updated_;
    QDateTime packetReceivedAt_;
    QElapsedTimer monitorClock_;
    qint64 lastDataAt_=-1;
    bool receptionFailed_=false, alarmsPaused_=false, notificationFailureLogged_=false;
    QString activeCommandError_;
    QString automationProblem_, pendingAutomationSource_, pendingOccurrenceKey_;
    quint64 commandFailureId_=0;
    AlertLatch alarmLatch_;
    QList<Alert> activeAlerts_;
    MonitorBar* monitorBar_;
    QJsonObject status_, pending_;
    QString error_, notice_, commandError_;
    std::array<PanelButton*,8> controls_{};
    QWidget* emblemBar_;
    QGridLayout* emblemLayout_;
    std::array<Emblem*,9> emblems_{};
    QWidget* valueArea_;
    QGridLayout* valueLayout_;
    std::array<QLabel*,5> values_{};
    QSystemTrayIcon* tray_ = nullptr;
};
