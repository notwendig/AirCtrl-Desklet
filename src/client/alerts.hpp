/**
 * @file alerts.hpp
 * @brief Data-freshness, device-alarm, and notification presentation types.
 */
#pragma once
#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QWidget>
#include <QDBusMessage>
#include <QDateTime>
#include <QTimer>

/** @brief Severity assigned by local alert policy. */
enum class AlertLevel { None, Warning, Error };
/** @brief Connection freshness derived from monotonic packet age. */
enum class DataFreshness { Waiting, Fresh, Aging, Stale, Blinking };
/** @brief Stable alert identity, severity, and localized presentation text. */
struct Alert {
    QString key;
    AlertLevel level;
    QString message;
};
/** @brief Classify a packet age using the configured warning thresholds. */
DataFreshness dataFreshness(qint64 seconds, int warningAfter, int staleAfter, int blinkAfter);
/** @brief Derive active alerts from an unchanged device status snapshot. */
QList<Alert> deviceAlerts(const QJsonObject& status);
/** @brief Format a list of alerts for diagnostics or a details dialog. */
QString alertReport(const QList<Alert>& alerts);
/** @brief Build the desktop notification message for an alarm incident. */
QDBusMessage alarmNotification(const QString& message, bool critical);

/**
 * @brief Suppress repeated notifications while preserving escalation and recurrence.
 *
 * One notification is emitted per incident. Escalation re-arms the incident,
 * and an alert that disappeared may notify again when it returns.
 */
class AlertLatch {
public:
    /** @brief Return alerts that became newly reportable in this update. */
    QList<Alert> update(const QList<Alert>& active);
    /** @brief Mark the currently active alerts as acknowledged by the user. */
    void acknowledge(const QList<Alert>& active);
    /** @brief Test whether the exact active alert severity is acknowledged. */
    bool acknowledged(const Alert& alert) const;
private:
    QMap<QString,int> notified_, acknowledged_;
};

/** @brief Paints independent data-age and alarm indicators. */
class MonitorBar : public QWidget {
    Q_OBJECT
public:
    explicit MonitorBar(QWidget* parent=nullptr);
    /** @brief Replace the complete monitoring state and schedule repainting. */
    void setState(const QDateTime& receivedAt, DataFreshness freshness, const QList<Alert>& alerts,
                  bool acknowledged, const QString& detail);
    QSize sizeHint() const override;
    QColor ageColor() const;
    QColor alarmColor() const;
    QRectF ageCircle() const;
    QRectF alarmCircle() const;
    QString ageText() const { return receivedAt_.isValid() ? receivedAt_.toString("HH:mm") : QString("—"); }
    DataFreshness freshness() const { return freshness_; }
    void setLightSurface(bool light) { if(lightSurface_!=light) { lightSurface_=light; update(); } }
    QString alarmText() const;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    int diameter() const;
    QDateTime receivedAt_;
    QTimer blinkTimer_;
    bool blinkVisible_=true;
    bool lightSurface_=true;
    DataFreshness freshness_=DataFreshness::Waiting;
    QList<Alert> alerts_;
    bool acknowledged_=false;
};
