/**
 * @file controller.hpp
 * @brief Asynchronous TCP client used by the desklet and Lua automation.
 */
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>

/**
 * @brief Maintains one client connection to an AirControl server.
 *
 * Controller never opens a UDP socket and never addresses the Philips device.
 * Commands are correlated by identifier and become complete only after a new
 * confirmed status snapshot arrives from the server.
 */
class Controller : public QObject {
    Q_OBJECT
public:
    static constexpr int ObserveRequestSeconds = 60;
    static constexpr int ObserveIdleSeconds = 90;
    /** @brief Construct a controller, optionally with a test-server executable. */
    explicit Controller(QString serverExecutable, QObject* parent = nullptr);
    ~Controller() override;
    /** @brief Configure the TCP endpoint and retry interval. */
    void configure(QString serverHost, int serverPort, int reconnectSeconds);
    /** @brief Begin connecting and reconnecting to the configured server. */
    void start();
    /** @brief Stop timers, abort the TCP connection, and clear pending state. */
    void stop();
    bool busy() const { return busy_; }
    bool observing() const { return hasStatus_ && socket_.state() == QAbstractSocket::ConnectedState; }
    quint64 statusCount() const { return statusCount_; }
    quint64 observationStarts() const { return observationStarts_; }
    QString observationProgress() const { return progress_; }
    QString host() const { return serverHost_; }
    QString backendPath() const { return executable_; }
    QString serverEndpoint() const { return serverHost_+":"+QString::number(serverPort_); }
    /** @brief Override the command-response watchdog; intended for tests. */
    void setWatchdogInterval(int milliseconds);
    /** @brief Override the post-command status timeout; intended for tests. */
    void setConfirmationTimeout(int milliseconds);
    /** @brief Override the reconnect delay; intended for tests. */
    void setReconnectDelay(int milliseconds);
public slots:
    /** @brief Ask the server to rebuild its device I/O session. */
    void refresh();
    /** @brief Request a power-state change through the server. */
    void setPower(bool on);
    /** @brief Request a target-humidity change through the server. */
    void setHumidity(int percent);
    /** @brief Submit an allow-listed set of panel values. */
    void setPanelValues(const QJsonObject& values);
signals:
    /** @brief Emitted for every valid status packet, before state processing. */
    void statusPacketReceived();
    /** @brief Emitted when a confirmed status snapshot may be displayed. */
    void statusReceived(QJsonObject status);
    /** @brief Reports whether a command is awaiting response or confirmation. */
    void busyChanged(bool busy);
    /** @brief Reports server or device-session connectivity failures. */
    void failed(QString reason);
    /** @brief Reports a rejected, timed-out, or unconfirmed control command. */
    void commandFailed(QString reason);
    /** @brief Indicates that the server accepted the pending command. */
    void controlAccepted();
private:
    void connectServer();
    void launchServer();
    void readServer();
    void handleEnvelope(const QJsonObject& envelope);
    void send(const QJsonObject& object);
    void connectionFailed(const QString& reason);
    void launchWrite(const QJsonObject& values);
    void failCommand(const QString& reason);
    void setBusy(bool busy);
    QString addressError() const;

    QString executable_, serverHost_;
    int serverPort_ = 5680, reconnectMs_ = 10000;
    int writeMs_ = 25000, confirmationMs_ = 90000;
    bool active_ = false, busy_ = false, hasStatus_ = false;
    bool awaitingConfirmation_ = false, launchAttempted_ = false, failureReported_ = false;
    bool refreshScheduled_ = false;
    quint64 statusCount_ = 0, observationStarts_ = 0;
    quint64 nextCommandId_ = 1, pendingCommandId_ = 0;
    QString progress_;
    QTcpSocket socket_;
    QTimer reconnect_, connectWatchdog_, writeWatchdog_, confirmation_;
    QByteArray stream_;
};
