#pragma once
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QWidget>

enum class EmblemIcon {
    Sleep, Auto, Allergen, Purify, Humidify, Filter, Water, Clean,
    PM25, IAI, Wifi, ChildLock, Fan, Timer
};

struct EmblemState {
    QString id;
    EmblemIcon icon;
    QString description;
    QString badge;
    bool warning = false;
};

// Derived exclusively from the last confirmed snapshot, never pending commands.
QList<EmblemState> currentEmblems(const QJsonObject& status, bool connected);

class Emblem : public QWidget {
    Q_OBJECT
public:
    explicit Emblem(QWidget* parent = nullptr);
    void configure(const EmblemState& state, const QColor& foreground, const QFont& font,
                   bool connected, bool hasStatus);
    EmblemIcon icon() const { return state_.icon; }
    QColor ink() const { return ink_; }
    bool stale() const { return stale_; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    EmblemState state_{};
    QColor ink_;
    bool stale_ = false;
    bool disconnected_ = false;
};
