#pragma once
#include <QObject>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>

class Controller : public QObject {
    Q_OBJECT
public:
    static constexpr int ObserveRequestSeconds = 60;
    static constexpr int ObserveIdleSeconds = 90;
    explicit Controller(QString serverExecutable, QObject* parent = nullptr);
    ~Controller() override;
    void configure(QString serverHost, int serverPort, int reconnectSeconds);
    void start();
    void stop();
    bool busy() const { return busy_; }
    bool observing() const { return hasStatus_ && socket_.state() == QAbstractSocket::ConnectedState; }
    quint64 statusCount() const { return statusCount_; }
    quint64 observationStarts() const { return observationStarts_; }
    QString observationProgress() const { return progress_; }
    QString host() const { return serverHost_; }
    QString backendPath() const { return executable_; }
    QString serverEndpoint() const { return serverHost_+":"+QString::number(serverPort_); }
    void setWatchdogInterval(int milliseconds);
    void setConfirmationTimeout(int milliseconds);
    void setReconnectDelay(int milliseconds);
public slots:
    void refresh();
    void setPower(bool on);
    void setHumidity(int percent);
    void setPanelValues(const QJsonObject& values);
signals:
    void statusPacketReceived();
    void statusReceived(QJsonObject status);
    void busyChanged(bool busy);
    void failed(QString reason);
    void commandFailed(QString reason);
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
