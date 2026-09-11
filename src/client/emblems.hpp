/**
 * @file emblems.hpp
 * @brief Derived status emblems and local filter-maintenance policy.
 */
#pragma once
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QWidget>

/** @brief Symbol identities that may be derived from a confirmed snapshot. */
enum class EmblemIcon {
    Sleep, Auto, Allergen, Purify, Humidify, Filter, Water, Clean,
    PM25, IAI, Wifi, ChildLock, Fan, Timer
};

/** @brief Presentation state for one status emblem. */
struct EmblemState {
    QString id;
    EmblemIcon icon;
    QString description;
    QString badge;
    bool warning = false;
};

/** Local desklet policy, not a documented Philips firmware threshold. */
inline constexpr int FilterWarningHours = 120;
/** @brief A filter warning derived from documented counter fields. */
struct FilterNotice {
    QString key;
    QString message;
    bool due = false;
};
/** @brief Derive maintenance notices without changing any device value. */
QList<FilterNotice> filterNotices(const QJsonObject& status);

/**
 * @brief Derive visible emblems exclusively from the last confirmed snapshot.
 * @note Pending commands are deliberately ignored.
 */
QList<EmblemState> currentEmblems(const QJsonObject& status, bool connected);

/** @brief Custom-painted view of a single derived device state. */
class Emblem : public QWidget {
    Q_OBJECT
public:
    explicit Emblem(QWidget* parent = nullptr);
    /** @brief Apply derived state and appearance for the next paint event. */
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
