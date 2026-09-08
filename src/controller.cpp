#include "controller.hpp"
#include "controlvalues.hpp"
#include "ipc.hpp"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>

Controller::Controller(QString serverExecutable, QObject* parent)
    : QObject(parent), executable_(std::move(serverExecutable)), socketPath_(airctrlSocketPath()) {
    for (auto* timer : {&reconnect_, &connectWatchdog_, &writeWatchdog_, &confirmation_})
        timer->setSingleShot(true);
    connect(&reconnect_, &QTimer::timeout, this, &Controller::connectServer);
    connect(&connectWatchdog_, &QTimer::timeout, this, [this] {
        socket_.abort();
        connectionFailed("Keine Verbindung zum lokalen AirControl-Server.");
    });
    connect(&writeWatchdog_, &QTimer::timeout, this, [this] {
        failCommand("Keine Serverantwort auf den Schaltbefehl · Ausgang unbekannt; keine automatische Wiederholung.");
    });
    connect(&confirmation_, &QTimer::timeout, this, [this] {
        failCommand("Keine neue Statusbestätigung nach dem Schaltbefehl · keine automatische Wiederholung.");
    });
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        connectWatchdog_.stop();
        failureReported_ = false;
        serverConfigured_ = false;
        progress_ = "Mit lokalem AirControl-Server verbunden";
        sendConfigure();
    });
    connect(&socket_, &QLocalSocket::readyRead, this, &Controller::readServer);
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
        connectWatchdog_.stop();
        launchAttempted_ = false;
        serverConfigured_ = false;
        hasStatus_ = false;
        if (busy_) failCommand("Serververbindung wurde während des Schaltbefehls beendet · keine Wiederholung.");
        if (active_) connectionFailed("Verbindung zum lokalen AirControl-Server wurde beendet.");
    });
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
        if (!active_ || error == QLocalSocket::PeerClosedError) return;
        if (!launchAttempted_ && (error == QLocalSocket::ServerNotFoundError ||
                                  error == QLocalSocket::ConnectionRefusedError)) {
            launchServer();
            reconnect_.start(100);
            return;
        }
        connectionFailed("Lokaler AirControl-Server nicht erreichbar: " + socket_.errorString());
    });
}

Controller::~Controller() { stop(); }

void Controller::configure(QString host, int port, int reconnectSeconds) {
    host_ = host.trimmed();
    port_ = port;
    reconnectMs_ = qBound(5, reconnectSeconds, 300) * 1000;
    if (socket_.state() == QLocalSocket::ConnectedState) sendConfigure();
}
void Controller::setWatchdogInterval(int ms) { writeMs_ = qMax(50, ms); }
void Controller::setObservationWatchdogs(int startup, int idle) {
    requestMs_ = qMax(50, startup);
    idleMs_ = qMax(50, idle);
    if (socket_.state() == QLocalSocket::ConnectedState) sendConfigure();
}
void Controller::setConfirmationTimeout(int ms) { confirmationMs_ = qMax(50, ms); }
void Controller::setReconnectDelay(int ms) {
    reconnectMs_ = qMax(50, ms);
    if (socket_.state() == QLocalSocket::ConnectedState) sendConfigure();
}
void Controller::setBusy(bool busy) {
    if (busy_ == busy) return;
    busy_ = busy;
    emit busyChanged(busy);
}
QString Controller::addressError() const {
    if (host_.isEmpty() || port_ < 1 || port_ > 65535)
        return "Ungültiger Hostname oder ungültige IP-Adresse.";
    if (!QFileInfo(executable_).isExecutable()) return "AirControl-Server fehlt: " + executable_;
    return {};
}
void Controller::start() {
    if (active_) return;
    active_ = true;
    launchAttempted_ = false;
    connectServer();
}
void Controller::stop() {
    active_ = false;
    refreshScheduled_ = false;
    reconnect_.stop();
    connectWatchdog_.stop();
    writeWatchdog_.stop();
    confirmation_.stop();
    awaitingConfirmation_ = false;
    pendingCommandId_ = 0;
    hasStatus_ = false;
    serverConfigured_ = false;
    stream_.clear();
    socket_.abort();
    setBusy(false);
}
void Controller::connectServer() {
    if (!active_ || socket_.state() != QLocalSocket::UnconnectedState) return;
    const auto error = addressError();
    if (!error.isEmpty()) {
        connectionFailed(error);
        return;
    }
    progress_ = "Verbinde mit lokalem AirControl-Server";
    // Start first: connecting to an already listening local server may emit
    // connected() before connectToServer() returns. Starting the watchdog
    // afterwards would arm a stale timer that aborts the healthy socket.
    connectWatchdog_.start(3000);
    socket_.connectToServer(socketPath_, QIODevice::ReadWrite);
}
void Controller::launchServer() {
    launchAttempted_ = true;
    const QStringList arguments{"--socket", socketPath_, "-H", host_, "-P", QString::number(port_),
        "--reconnect-ms", QString::number(reconnectMs_), "--request-ms", QString::number(requestMs_),
        "--idle-ms", QString::number(idleMs_)};
    // A detached server must not inherit CTest's stdout/stderr pipes. Otherwise
    // CTest waits for the long-lived server even after the test process exited.
    QProcess server;
    server.setProgram(executable_);
    server.setArguments(arguments);
    server.setStandardOutputFile(QProcess::nullDevice());
    server.setStandardErrorFile(QProcess::nullDevice());
    if (!server.startDetached())
        connectionFailed("AirControl-Server konnte nicht gestartet werden: " + executable_);
}
void Controller::connectionFailed(const QString& reason) {
    hasStatus_ = false;
    progress_ = "Warte auf lokalen Server";
    if (!failureReported_) {
        failureReported_ = true;
        emit failed(reason);
    }
    if (active_ && !reconnect_.isActive()) reconnect_.start(reconnectMs_);
}
void Controller::refresh() {
    if (!active_ || busy_ || refreshScheduled_) return;
    refreshScheduled_ = true;
    QTimer::singleShot(0, this, [this] {
        refreshScheduled_ = false;
        if (!active_ || busy_) return;
        if (socket_.state() == QLocalSocket::ConnectedState) {
            send({{"_airctrl", "refresh"}});
            progress_ = "Geräte-I/O wird im Server neu aufgebaut";
        } else {
            reconnect_.stop();
            connectServer();
        }
    });
}
void Controller::sendConfigure() {
    send({{"_airctrl", "configure"}, {"host", host_}, {"port", port_},
          {"reconnect_ms", reconnectMs_}, {"request_ms", requestMs_}, {"idle_ms", idleMs_}});
}
void Controller::send(const QJsonObject& object) {
    if (socket_.state() != QLocalSocket::ConnectedState) return;
    socket_.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
}
void Controller::readServer() {
    stream_ += socket_.readAll();
    for (;;) {
        const auto newline = stream_.indexOf('\n');
        if (newline < 0) break;
        if (newline > 1024 * 1024) {
            socket_.abort();
            connectionFailed("IPC-Nachricht des Servers ist zu groß.");
            return;
        }
        const auto line = stream_.left(newline).trimmed();
        stream_.remove(0, newline + 1);
        if (line.isEmpty()) continue;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            socket_.abort();
            connectionFailed("Ungültige IPC-Nachricht des Servers.");
            return;
        }
        handleEnvelope(document.object());
    }
    if (stream_.size() > 1024 * 1024) {
        socket_.abort();
        connectionFailed("Unvollständige IPC-Nachricht des Servers ist zu groß.");
    }
}
void Controller::handleEnvelope(const QJsonObject& envelope) {
    const auto kind = envelope.value("_airctrl").toString();
    if (kind == "state") {
        observationStarts_ = envelope.value("starts").toVariant().toULongLong();
        const auto state = envelope.value("state").toString();
        if (state == "error") {
            hasStatus_ = false;
            auto reason = envelope.value("error").toString().trimmed();
            if (busy_) failCommand("Geräte-I/O des Servers wurde während des Schaltbefehls unterbrochen · keine Wiederholung.");
            connectionFailed(reason.isEmpty() ? "Geräteverbindung des Servers fehlgeschlagen." : reason);
        } else if (state == "connecting") {
            hasStatus_ = false;
            progress_ = "Server baut Geräte-I/O auf";
        }
        return;
    }
    if (kind == "configured") { serverConfigured_ = true; return; }
    if (kind == "pong") return;
    if (kind == "error") {
        connectionFailed(envelope.value("error").toString());
        return;
    }
    if (kind == "control") {
        const auto id = envelope.value("id").toVariant().toULongLong();
        if (!busy_ || id == 0 || id != pendingCommandId_) return;
        writeWatchdog_.stop();
        pendingCommandId_ = 0;
        if (!envelope.value("ok").toBool()) {
            auto reason = envelope.value("error").toString().trimmed();
            failCommand(reason.isEmpty() ? "Schaltbefehl abgelehnt oder nicht bestätigt." : reason);
            return;
        }
        awaitingConfirmation_ = true;
        confirmation_.start(confirmationMs_);
        emit controlAccepted();
        return;
    }
    if (kind != "status" || !envelope.value("data").isObject() ||
        envelope.value("data").toObject().isEmpty()) {
        connectionFailed("Unbekannte Meldung des lokalen AirControl-Servers.");
        return;
    }
    if (!serverConfigured_) return; // ignore a cache from a different prior configuration
    const auto status = envelope.value("data").toObject();
    hasStatus_ = true;
    failureReported_ = false;
    ++statusCount_;
    progress_ = "Server und Geräte-I/O aktiv";
    emit statusPacketReceived();
    if (busy_ && !awaitingConfirmation_) return;
    if (awaitingConfirmation_) {
        awaitingConfirmation_ = false;
        confirmation_.stop();
        setBusy(false);
    }
    emit statusReceived(status);
}
void Controller::setPower(bool on) { setPanelValues({{"pwr", on ? "1" : "0"}}); }
void Controller::setHumidity(int percent) { setPanelValues({{"rhset", percent}}); }
void Controller::setPanelValues(const QJsonObject& values) {
    if (busy_ || values.isEmpty()) return;
    const auto problem = controlValuesError(values);
    if (!problem.isEmpty()) {
        failCommand(problem);
        return;
    }
    launchWrite(values);
}
void Controller::launchWrite(const QJsonObject& values) {
    if (busy_) return;
    const auto error = addressError();
    if (!error.isEmpty()) {
        failCommand(error);
        return;
    }
    if (!active_ || socket_.state() != QLocalSocket::ConnectedState || !hasStatus_) {
        failCommand("Keine aktive Geräteverbindung im AirControl-Server.");
        return;
    }
    awaitingConfirmation_ = false;
    pendingCommandId_ = nextCommandId_++;
    setBusy(true);
    send({{"_airctrl", "control"}, {"id", static_cast<qint64>(pendingCommandId_)}, {"values", values}});
    writeWatchdog_.start(writeMs_);
}
void Controller::failCommand(const QString& reason) {
    pendingCommandId_ = 0;
    awaitingConfirmation_ = false;
    writeWatchdog_.stop();
    confirmation_.stop();
    setBusy(false);
    emit commandFailed(reason);
}
