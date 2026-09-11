/**
 * @file panel.hpp
 * @brief Custom-painted controls for the eight physical device functions.
 */
#pragma once
#include <QPushButton>
#include <QColor>

/** @brief Icon identities used by the compact panel buttons. */
enum class PanelIcon { Power, ChildLock, Auto, Fan, Humidity, Light, Function, Timer };

/** @brief A fixed-size, theme-aware device control button. */
class PanelButton : public QPushButton {
public:
    /** @brief Construct a button with its icon identity and accessible name. */
    PanelButton(PanelIcon icon, const QString& name, QWidget* parent);
    QSize sizeHint() const override { return {29,31}; }
    void setForeground(const QColor& color) { foreground_ = color; update(); }
    void setStatusColor(const QColor& color) { statusColor_ = color; update(); }
    QColor statusColor() const { return statusColor_; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    PanelIcon icon_;
    QColor foreground_{"#222222"};
    QColor statusColor_;
};
