#pragma once
#include "controller.hpp"
#include "preferences.hpp"
#include "panel.hpp"
#include <QDateTime>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QWidget>
#include <QGridLayout>
#include <array>

class Desklet : public QWidget {
    Q_OBJECT
public:
    Desklet(Preferences preferences, QString backend, bool demo = false);
    void start();
    void showSettings();
    void showDetails();
    void applyStatus(const QJsonObject& status);
    void setConnectionError(const QString& error);
    void showAndPosition();
protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void closeEvent(QCloseEvent*) override;
    virtual bool startNativeMove();
private:
    void updateControls();
    void updateFooter();
    void updateValues();
    void applyAppearance();
    void saveAppearance();
    void resizeToContent();
    void applyWindowMode();
    void requestMenu(const QPoint& point);
    void openMenu(const QPoint& point);
    void showPositionDialog();
    void openControl(int index);
    void sendValues(const QJsonObject& values);
    void rememberPosition();
    Preferences preferences_;
    Controller controller_;
    bool demo_ = false;
    bool waylandSession_ = false;
    bool connected_ = false;
    bool awaitingConfirmation_ = false;
    bool leftPressed_ = false;
    bool rightPressed_ = false;
    bool mouseMoved_ = false;
    bool menuPending_ = false;
    bool menuOpen_ = false;
    QPoint pressPosition_;
    QPoint dragOffset_;
    QDateTime updated_;
    QJsonObject status_, pending_;
    QString error_, notice_;
    std::array<PanelButton*,8> controls_{};
    QWidget* valueArea_;
    QGridLayout* valueLayout_;
    std::array<QLabel*,5> values_{};
    QSystemTrayIcon* tray_ = nullptr;
};
