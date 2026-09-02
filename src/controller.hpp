#pragma once
#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

class Controller : public QObject {
    Q_OBJECT
public:
    explicit Controller(QString executable, QObject* parent = nullptr);
    ~Controller() override;
    void configure(QString host, int port, int intervalSeconds);
    void start();
    void stop();
    bool busy() const { return busy_; }
    QString host() const { return host_; }
    QString backendPath() const { return executable_; }
    void setWatchdogInterval(int milliseconds); // permits short deterministic tests
public slots:
    void refresh();
    void setPower(bool on, bool interruptRead = false);
    void setHumidity(int percent);
    void setPanelValues(const QJsonObject& values);
signals:
    void statusReceived(QJsonObject status);
    void busyChanged(bool busy);
    void failed(QString reason);
    void controlAccepted();
private:
    enum class Operation { Read, Write };
    void launch(Operation operation, const QStringList& tail);
    void finished(int exitCode, QProcess::ExitStatus exitStatus);
    void fail(const QString& reason);
    void setBusy(bool busy);
    QString executable_;
    QString host_ = "192.168.77.5";
    int port_ = 5683;
    int intervalMs_ = 10000;
    int watchdogMs_ = 25000; // 10 s synchronization + 10 s status/control + startup margin
    int readAttempt_ = 0;
    bool busy_ = false;
    bool active_ = false;
    bool timedOut_ = false;
    bool oversized_ = false;
    bool powerInterrupt_ = false;
    Operation operation_ = Operation::Read;
    QProcess process_;
    QTimer poll_;
    QTimer watchdog_;
    QTimer verify_;
    QTimer readRetry_;
    QByteArray output_;
    QByteArray error_;
    QStringList queuedWrite_; // one user command may wait for an in-flight read
};
