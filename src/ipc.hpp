#pragma once

#include <QString>

// Per-user local IPC endpoint. AIRCTRL_SOCKET is intentionally supported for
// isolated tests and parallel development builds.
QString airctrlSocketPath();

