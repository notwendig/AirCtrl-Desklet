#pragma once
#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

class Controller : public QObject {
    Q_OBJECT
public:
    static constexpr int ObserveRequestSeconds = 60;
    static constexpr int ObserveIdleSeconds = 90;
    explicit Controller(QString executable, QObject* parent = nullptr);
    ~Controller() override;
    void configure(QString host, int port, int reconnectSeconds);
    void start();
    void stop();
    bool busy() const { return busy_; } // user command + confirmation only
    bool observing() const { return hasStatus_ && !observerStopping_; }
    quint64 statusCount() const { return statusCount_; }
    quint64 observationStarts() const { return observationStarts_; }
    QString observationProgress() const { return progress_; }
    QString host() const { return host_; }
    QString backendPath() const { return executable_; }
    // Short deterministic tests; backend CLI limits remain unchanged.
    void setWatchdogInterval(int milliseconds);
    void setObservationWatchdogs(int startupMs, int idleMs);
    void setConfirmationTimeout(int milliseconds);
    void setReconnectDelay(int milliseconds);
public slots:
    void refresh(); // explicit reconnect (F5), never periodic polling
    void setPower(bool on);
    void setHumidity(int percent);
    void setPanelValues(const QJsonObject& values);
signals:
    void statusPacketReceived(); // every complete valid status, even while a write is pending
    void statusReceived(QJsonObject status);
    void busyChanged(bool busy);
    void failed(QString reason); // observation/connection failure
    void commandFailed(QString reason); // does not invalidate healthy observation
    void controlAccepted();
private:
    void launchObserver();
    void stopObserver();
    void readObserver();
    void abortObserver(const QString& reason);
    void observerFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void observationFailed(const QString& reason);
    void launchWrite(const QStringList& tail);
    void writeFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void failCommand(const QString& reason);
    void setBusy(bool busy);
    QString addressError() const;
    QString executable_, host_ = "192.168.77.5";
    int port_ = 5683, reconnectMs_ = 10000;
    int startupMs_ = 125000; // 60 s sync + 60 s first status + startup reserve
    int idleMs_ = 95000;    // backend has a 90 s observation idle timeout
    int writeMs_ = 25000, confirmationMs_ = 90000;
    bool active_ = false, busy_ = false, hasStatus_ = false;
    bool observerStopping_ = false, restartObserver_ = false, writerStopping_ = false;
    bool awaitingConfirmation_ = false;
    quint64 statusCount_ = 0, observationStarts_ = 0;
    QString progress_, observerProblem_, writerProblem_;
    QProcess observer_, writer_;
    QTimer reconnect_, observationWatchdog_, observerStopWatchdog_, writeWatchdog_, confirmation_;
    QByteArray stream_, observerError_, writerError_;
    qsizetype writerBytes_ = 0;
};
