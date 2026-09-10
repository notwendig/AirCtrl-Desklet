#pragma once

#include <QString>
#include <QtGlobal>

// Client endpoint. Environment overrides are intentionally supported for
// isolated tests; production settings are stored by Preferences.
QString defaultAirctrlServerHost();
quint16 defaultAirctrlServerPort();
