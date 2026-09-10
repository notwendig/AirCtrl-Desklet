#include "ipc.hpp"

QString defaultAirctrlServerHost() {
    const auto testHost=qEnvironmentVariable("AIRCTRL_TEST_SERVER_HOST").trimmed();
    return testHost.isEmpty() ? QStringLiteral("nadhh") : testHost;
}

quint16 defaultAirctrlServerPort() {
    bool ok=false;
    const auto value=qEnvironmentVariable("AIRCTRL_TEST_SERVER_PORT").toUInt(&ok);
    return ok && value>0 && value<=65535 ? static_cast<quint16>(value) : quint16(5680);
}
