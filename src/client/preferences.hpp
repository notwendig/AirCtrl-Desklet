/**
 * @file preferences.hpp
 * @brief Persistent client, alarm, automation, and appearance preferences.
 */
#pragma once
#include <QString>
#include <QPoint>
#include <QColor>
#include <QFont>
#include <QStringList>
/** @brief Complete per-user configuration consumed by the desklet client. */
struct Preferences {
    /** @brief Construct preferences with the documented server defaults. */
    Preferences();
    ///< AirControl TCP server hostname; never the Philips device hostname.
    QString serverHost;
    ///< AirControl TCP server port.
    int serverPort;
    ///< Delay before the client retries a lost server connection.
    int serverReconnectSeconds = 10;
    int ageWarningSeconds = 45;
    int ageStaleSeconds = 90;
    bool desktopAlarms = true;
    bool alarmSound = false;
    bool automationEnabled = false;
    bool desktop = true;
    bool hideDecoration = true;
    bool locked = false;
    QPoint position{-1, -1};
    QColor background{"#f1f1f1"};
    QColor foreground{"#222222"};
    ///< Background only: 0 is opaque and 100 is fully transparent.
    int transparency = 0;
    QFont valueFont{"DejaVu Sans", 10};
    QStringList visibleValues{"rh", "rhset", "temp", "pm25"};
    /** @brief Load and validate preferences from the platform settings store. */
    static Preferences load();
    /** @brief Atomically update the per-user settings store. */
    void save() const;
};

/** @brief Return the desktop-entry path used for per-user autostart. */
QString autostartPath();

/**
 * @brief Enable or disable per-user desklet autostart.
 * @param enabled Whether the desktop entry should exist.
 * @param error Optional destination for a localized failure description.
 * @return `true` when the requested state was applied.
 */
bool setAutostart(bool enabled, QString* error = nullptr);
