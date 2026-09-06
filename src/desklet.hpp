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

class Desklet : public QWidget {
    Q_OBJECT
public:
    Desklet(Preferences preferences, QString backend, bool demo = false);
    void start();
    void showSettings();
    void showDetails();
    void showAlarms();
    void showAlarmSettings();
    void showAutomationSettings();
    void acknowledgeAlarms();
    qint64 dataAgeSeconds() const;
    void applyStatus(const QJsonObject& status);
    void setConnectionError(const QString& error);
    void showAndPosition();
signals:
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
