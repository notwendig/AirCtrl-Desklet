#include "ipc.hpp"

#include <QDir>
#include <QStandardPaths>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

QString airctrlSocketPath() {
    const auto overridePath = qEnvironmentVariable("AIRCTRL_SOCKET").trimmed();
    if (!overridePath.isEmpty()) return overridePath;

    auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR").trimmed();
    if (runtime.isEmpty()) {
#ifdef Q_OS_UNIX
        runtime = QDir::tempPath() + "/airctrl-" + QString::number(::getuid());
#else
        runtime = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/airctrl";
#endif
    }
    return QDir(runtime).filePath("airctrl-desklet/server.sock");
}
