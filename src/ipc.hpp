/**
 * @file ipc.hpp
 * @brief Shared defaults for the public AirControl TCP endpoint.
 */
#pragma once

#include <QString>
#include <QtGlobal>

/**
 * @brief Return the default AirControl server hostname.
 *
 * Production clients default to `nadhh`. The `AIRCTRL_TEST_SERVER_HOST`
 * override exists solely so isolated tests can use a loopback endpoint.
 */
QString defaultAirctrlServerHost();

/**
 * @brief Return the default AirControl TCP port.
 * @return A valid port in the range 1..65535; normally 5680.
 */
quint16 defaultAirctrlServerPort();
