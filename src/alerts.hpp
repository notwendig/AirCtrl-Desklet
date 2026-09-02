#pragma once
#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QWidget>
#include <QDBusMessage>

enum class AlertLevel { None, Warning, Error };
enum class DataFreshness { Waiting, Fresh, Aging, Stale, Disconnected };
struct Alert {
    QString key;
    AlertLevel level;
    QString message;
};
DataFreshness dataFreshness(qint64 seconds, bool failed, int warningAfter, int staleAfter);
QList<Alert> deviceAlerts(const QJsonObject& status);
QString alertReport(const QList<Alert>& alerts);
QDBusMessage alarmNotification(const QString& message, bool critical);

// One notification per incident; escalation re-arms, a cleared incident can recur.
class AlertLatch {
public:
    QList<Alert> update(const QList<Alert>& active);
    void acknowledge(const QList<Alert>& active);
    bool acknowledged(const Alert& alert) const;
private:
    QMap<QString,int> notified_, acknowledged_;
};

class MonitorBar : public QWidget {
    Q_OBJECT
public:
    explicit MonitorBar(QWidget* parent=nullptr);
    void setState(qint64 seconds, DataFreshness freshness, const QList<Alert>& alerts,
                  bool acknowledged, const QString& detail);
    QSize sizeHint() const override;
    QColor ageColor() const;
    QString ageText() const { return seconds_<0 ? QString("— s") : QString::number(seconds_)+" s"; }
    DataFreshness freshness() const { return freshness_; }
    QString alarmText() const;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    int ageWidth() const;
    qint64 seconds_=-1;
    DataFreshness freshness_=DataFreshness::Waiting;
    QList<Alert> alerts_;
    bool acknowledged_=false;
};
