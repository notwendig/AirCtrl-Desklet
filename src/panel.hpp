#pragma once
#include <QPushButton>
#include <QColor>

enum class PanelIcon { Power, ChildLock, Auto, Fan, Humidity, Light, Function, Timer };

class PanelButton : public QPushButton {
public:
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
